#include "elog_segmented_file_target.h"

#include "elog_def.h"

#ifndef ELOG_MSVC
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

#include <filesystem>
#include <thread>

#include "elog_system.h"

namespace elog {

static thread_local FILE* m_usedSegment;

#ifdef ELOG_MSVC
static bool scanDirFilesMsvc(const char* dirPath, std::vector<std::string>& fileNames) {
    // prepare search pattern
    std::string searchPattern = dirPath;
    searchPattern += "\\*";

    // begin search for files
    WIN32_FIND_DATA findFileData = {};
    HANDLE hFind = FindFirstFile(dirPath, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) {
        ELOG_WIN32_ERROR(FindFirstFile, "Failed to search for files in directory: %s", dirPath);
        return false;
    }

    // collect all files
    do {
        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE ||
            findFileData.dwFileAttributes & FILE_ATTRIBUTE_NORMAL) {
            fileNames.push_back(findFileData.cFileName);
        }
    } while (FindNextFile(hFind, &findFileData));

    // check for error
    DWORD errCode = GetLastError();
    if (errCode != ERROR_NO_MORE_FILES) {
        ELOG_SYS_ERROR_NUM(FindNextFile, errCode, "Failed to search for next file in directory: %s",
                           dirPath);
        return false;
    }

    return true;
}
#endif

#ifdef ELOG_MINGW
inline bool isRegularFile(const char* path, bool& res) {
    struct stat pathStat;
    if (stat(path, &pathStat) == -1) {
        int errCode = errno;
        ELOG_SYS_ERROR(stat, "Failed to check file %s status: %d", path, errCode);
        return false;
    }
    res = S_ISREG(pathStat.st_mode);
    return true;
}
#endif

#ifdef ELOG_GCC
static bool scanDirFilesGcc(const char* dirPath, std::vector<std::string>& fileNames) {
    DIR* dirp = opendir(dirPath);
    if (dirp == nullptr) {
        int errCode = errno;
        ELOG_SYS_ERROR(opendir, "Failed to open directory %s for reading: %d", dirPath, errCode);
        return false;
    }

    struct dirent* dir = nullptr;
#ifdef ELOG_MINGW
    std::string basePath = dirPath;
    while ((dir = readdir(dirp)) != nullptr) {
        bool isRegular = false;
        if (!isRegularFile((basePath + "/" + dir->d_name).c_str(), isRegular)) {
            closedir(dirp);
            return false;
        }
        if (isRegular) {
            fileNames.push_back(dir->d_name);
        }
    }
#else
    while ((dir = readdir(dirp)) != nullptr) {
        if (dir->d_type == DT_REG) {
            fileNames.push_back(dir->d_name);
        }
    }
#endif
    int errCode = errno;
    if (errCode != 0) {
        ELOG_SYS_ERROR(readdir, "Failed to list files in directory %s: %d", dirPath, errCode);
        closedir(dirp);
        return false;
    }
    if (closedir(dirp) < 0) {
        ELOG_SYS_ERROR(closedir, "Failed to terminate listing files in directory %s: %d", dirPath,
                       errCode);
        return false;
    }
    return true;
}
#endif

ELogSegmentedFileTarget::ELogSegmentedFileTarget(const char* logPath, const char* logName,
                                                 uint32_t segmentLimitMB,
                                                 ELogFlushPolicy* flushPolicy)
    : ELogTarget("segmented-file", flushPolicy),
      m_logPath(logPath),
      m_logName(logName),
      m_segmentLimitBytes(segmentLimitMB * 1024 * 1024),
      m_segmentCount(0),
      m_segmentSizeBytes(0),
      m_currentSegment(nullptr),
      m_entered(0),
      m_left(0) {
    // open current segment (start a new one if needed)
    openSegment();
}

ELogSegmentedFileTarget::~ELogSegmentedFileTarget() {}

bool ELogSegmentedFileTarget::startLogTarget() { return openSegment(); }

bool ELogSegmentedFileTarget::stopLogTarget() {
    if (m_currentSegment != nullptr) {
        if (fclose(m_currentSegment) == -1) {
            ELOG_SYS_ERROR(fopen, "Failed to close log segment");
            return false;
        }
        m_currentSegment = nullptr;
    }
    return true;
}

void ELogSegmentedFileTarget::logFormattedMsg(const std::string& formattedLogMsg) {
    // check if segment switch is required
    uint32_t msgSizeBytes = formattedLogMsg.length();
    uint32_t segmentSizeBytes = m_segmentSizeBytes.fetch_add(msgSizeBytes);
    if (segmentSizeBytes <= m_segmentLimitBytes &&
        (segmentSizeBytes + msgSizeBytes) >= m_segmentLimitBytes) {
        // open new segment
        // NOTE: in the meantime some log messages might still slip into currently being closed
        // segment, and that is ok
        advanceSegment();
    }

    // mark log start
    m_entered.fetch_add(1, std::memory_order_relaxed);
    FILE* currentSegment = m_currentSegment.load(std::memory_order_relaxed);
    if (fputs(formattedLogMsg.c_str(), currentSegment) == EOF) {
        ELOG_SYS_ERROR(fputs, "Failed to write to log file");
    }

    // we must remember the segment we used for logging, so that we can tell during flush it is
    // the same segment (so that if it changed, no flush will take place)
    m_usedSegment = currentSegment;

    // mark log finish
    m_left.fetch_add(1, std::memory_order_relaxed);
}

void ELogSegmentedFileTarget::flush() {
    FILE* currentSegment = m_currentSegment.load(std::memory_order_relaxed);
    if (m_usedSegment == currentSegment) {
        if (fflush(currentSegment) == EOF) {
            ELOG_SYS_ERROR(fflush, "Failed to flush log file");
        }
    }
}

bool ELogSegmentedFileTarget::openSegment() {
    // get segment count and last segment size
    uint32_t segmentCount = 0;
    uint32_t lastSegmentSizeBytes = 0;
    if (!getSegmentCount(segmentCount, lastSegmentSizeBytes)) {
        return false;
    }
    m_segmentCount = segmentCount;
    m_segmentSizeBytes = lastSegmentSizeBytes;

    // if last segment is too large, then start a new segment
    if (m_segmentSizeBytes > m_segmentLimitBytes) {
        ++m_segmentCount;
        m_segmentSizeBytes = 0;
    }

    // open the segment file for appending
    std::string segmentPath;
    formatSegmentPath(segmentPath);
    FILE* segment = fopen(segmentPath.c_str(), "a");
    if (segment == nullptr) {
        int errCode = errno;
        ELOG_SYS_ERROR(fopen, "Failed to open segment file %s: %d", segmentPath.c_str(), errCode);
        return false;
    }
    m_currentSegment.store(segment, std::memory_order_relaxed);
    return true;
}

bool ELogSegmentedFileTarget::getSegmentCount(uint32_t& segmentCount,
                                              uint32_t& lastSegmentSizeBytes) {
    // scan directory for all files with matching name:
    // <log-path>/<log-name>.log.<log-id>
    std::vector<std::string> fileNames;
    if (!scanDirFiles(m_logPath.c_str(), fileNames)) {
        return false;
    }

    int32_t maxSegmentIndex = -1;  // init with "no segments" value
    std::string lastSegmentName;
    std::vector<std::string>::iterator itr = fileNames.begin();
    while (itr != fileNames.end()) {
        const std::string fileName = *itr;
        int32_t segmentIndex = -1;
        if (!getSegmentIndex(fileName, segmentIndex)) {
            return false;
        }
        if (maxSegmentIndex < segmentIndex) {
            maxSegmentIndex = segmentIndex;
            lastSegmentName = fileName;
        }
        ++itr;
    }

    lastSegmentSizeBytes = 0;
    if (maxSegmentIndex >= 0) {
        if (!getFileSize((m_logPath + "/" + lastSegmentName).c_str(), lastSegmentSizeBytes)) {
            return false;
        }
    }
    return true;
}

bool ELogSegmentedFileTarget::scanDirFiles(const char* dirPath,
                                           std::vector<std::string>& fileNames) {
#ifdef ELOG_MSVC
    return scanDirFilesMsvc(dirPath, fileNames);
#else
    return scanDirFilesGcc(dirPath, fileNames);
#endif
}

bool ELogSegmentedFileTarget::getSegmentIndex(const std::string& fileName, int32_t& segmentIndex) {
    segmentIndex = -1;
    if (fileName.rfind(m_logName, 0) == 0) {  // begins-with check
        // extract segment index
        std::string logPrefix = m_logName + ".log";
        if (fileName.length() == logPrefix.length()) {
            // first segment with no index
            segmentIndex = 0;
            // otherwise we already have a segment with higher index
        } else {
            try {
                std::size_t pos = 0;
                segmentIndex = std::stoi(fileName.substr(logPrefix.length()), &pos);
                if (logPrefix.length() + pos != fileName.length()) {
                    // something is wrong, we have excess chars, so we ignore this segment
                    ELogSystem::reportError(
                        "Invalid segment file name, excess chars after segment index: %s",
                        fileName.c_str());
                    segmentIndex = -1;
                    return false;
                }
            } catch (std::exception& e) {
                ELOG_SYS_ERROR(
                    std::stoi,
                    "Invalid segment file name %s, segment index could not be parsed: %s",
                    fileName.c_str(), e.what());
                segmentIndex = -1;
                return false;
            }
        }
    }
    return true;
}

bool ELogSegmentedFileTarget::getFileSize(const char* filePath, uint32_t& fileSize) {
    try {
        std::filesystem::path p{filePath};
        fileSize = std::filesystem::file_size(p);
        return true;
    } catch (std::exception& e) {
        ELOG_SYS_ERROR(std::filesystem::file_size, "Failed to get size of segment %s: %s", filePath,
                       e.what());
        return false;
    }
}

void ELogSegmentedFileTarget::formatSegmentPath(std::string& segmentPath) {
    std::stringstream s;
    s << m_logPath << "/" << m_logName << ".log";
    if (m_segmentCount > 0) {
        s << "." << m_segmentCount.load(std::memory_order_relaxed);
    }
    segmentPath = s.str();
}

bool ELogSegmentedFileTarget::advanceSegment() {
    // we need to:
    // - open new segment
    // - switch segments
    // - busy wait until previous segment loggers are finished
    // - log message
    FILE* prevSegment = m_currentSegment.load(std::memory_order_relaxed);
    m_segmentCount.fetch_add(1, std::memory_order_relaxed);
    std::string segmentPath;
    formatSegmentPath(segmentPath);
    FILE* nextSegment = fopen(segmentPath.c_str(), "a");
    if (nextSegment == nullptr) {
        int errCode = errno;
        ELOG_SYS_ERROR(fopen, "Failed to open segment file %s: %d", segmentPath.c_str(), errCode);
        return false;
    }

    // it is theoretically possible that during log flooding we will not have time to close one
    // segment, when another segment already needs to be opened. this is ok, as several threads
    // will get "stuck" for a short while, each trying to close its segment

    // switch segments
    if (!m_currentSegment.compare_exchange_strong(prevSegment, nextSegment,
                                                  std::memory_order_relaxed)) {
        ELogSystem::reportError("Failed to switch log segment files, suspected log flooding");
        return false;
    }

    // now we need to wait until all current users of the previous segment are done
    // we start measuring from this point on, so we are on the safe side
    uint32_t entered = m_entered.load(std::memory_order_relaxed);
    while (entered >= m_left.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(0));
    }
    return true;
}

}  // namespace elog
