#include "elog_segmented_file_target.h"

#include "elog_def.h"

#ifndef ELOG_MSVC
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

#include <cinttypes>
#include <cstring>
#include <filesystem>
#include <thread>

#include "elog_error.h"

// some design notes
// =================
// when a segment changes we have a race condition to solve.
// we can make sure that only one thread gets to open a new log segment file.
// this will be the thread crossing segment boundaries.
// this can be inferred with an atomic counter and a bit of moduli calculation.
// the segment id can be inferred also from the counter.
// in reality, we just get the counter value before and after adding a message, and check the
// segment id to which each counter value belongs. if the value is not the same then a segment
// boundary has been crossed.
//
// so when this thread is busy opening a new segment, what should the other threads do?
// they can be allowed in to log into the old segment, which is probably ok, but will cause some out
// of order logging. another option is to have them wait, but that is unacceptable, since it will
// block the calling code. a third option would be to keep these messages in a queue and go on.
// when the segment is open, first the current thread logs its message (and it will be the only one
// taking a performance penalty), then all the queued messages will be drained. but how can we tell
// there are no more threads which are just about to put another message in the queue? we can take
// care of that by using atomic entered/left counters. So we record the entered counter AFTER
// creating the queue. then we wait that the left counter reaches that value. then we drain the
// queue. it is guaranteed at this point that no thread will post a log message to the queue.

// the last piece of the puzzle is, how can other logging threads know that some thread is busy
// opening a new segment, and therefore should queue their messages? the answer is by looking at
// another counter, which is current segment id. this counter will be advanced only after the new
// segment is ready for writing.

// another issue that needs to be addressed is the flush requests that may try to flush a segment
// that is being closed at the moment. to avoid this race condition, the flush request should check
// the log target state, as if it is logging a zero sized message. this way it is possible to tell
// whether the segment is now being replaced, in which case a flush request can be discarded, since
// the segment is automatically flushed during call to fclose() by the thread that advances the
// segment. Otherwise, the entered/left counters should be used to avoid race.

// regarding entered/left counters, it is best practice that these would be the very first and last
// calls in each compound operation that is prone to experience race conditions.

// another race condition that may arise due to log flooding and small segment size, is that several
// threads open a segment in parallel. the first problem that arises is that they all try to open
// the same segment. this can be solved by using 'open-segment-by-id' instead of 'advance-segment',
// so they all open a different segment in parallel. the second problem that arises is that they
// will all get stuck waiting for each other to increment the 'left' counter. In order to solve this
// issue we introduce another counter, namely the 'currently-opening-segment' counter. with this
// counter in hand, each thread that opens a new segment can check whether 'entered' +
// 'currently-opening-segment' == 'left'.

// the final problem that may arise is that all other threads push messages to the same pending
// queue. managing a queue per segment complicates matters even more, and becomes more increasingly
// like the quantum log target, and this is not the purpose here. so another approach would be to
// let each segment opening thread to log as many pending log messages as it can into the new
// segment that it just opened, and leave the excess to the next segment opening thread, that is in
// the meantime waiting for another counter to increase. this counter tells which segment opener
// should be currently pulling messages from the pending queue. we call this counter
// 'segment-opener-id', and it starts with the number (initial segment id + 1). when the segment
// opener with this id finishes pulling messages from the pending queue (either because the queue
// got empty, or the segment got full), it increments this counter. other segment opener threads
// will wait until the counter reaches the value of their segment id respectively.

// one last case is a message that does not fit within a single segment. this kind of message will
// take its own segment and will violate the segment size limitation.

// IMPLEMENTATION NOTE: The actual implementation is a bit different than noted above, but the same
// ideas are being used to solve all race conditions.
// Tests with log flooding show a small bloating of a few segments, while others are a bit smaller
// than the configured limit, but in any case, NO LOG MESSAGES ARE LOST, AND MESSAGE ORDER WITHIN
// EACH THREAD IS KEPT.

// Due to this complexities, it is often advised to put the segmented file log target behind a
// deferred log target, or any asynchronous logging scheme, so that the segmented file log target is
// accessed only from one thread, and this way there are no race conditions. another option is to
// use a lock.

namespace elog {
static thread_local FILE* m_usedSegment;
static const char* LOG_SUFFIX = ".log";

#ifdef ELOG_MSVC
static bool scanDirFilesMsvc(const char* dirPath, std::vector<std::string>& fileNames) {
    // prepare search pattern
    std::string searchPattern = dirPath;
    searchPattern += "\\*";

    // begin search for files
    WIN32_FIND_DATA findFileData = {};
    HANDLE hFind = FindFirstFile(dirPath, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) {
        ELOG_REPORT_WIN32_ERROR(FindFirstFile, "Failed to search for files in directory: %s",
                                dirPath);
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
        ELOG_REPORT_SYS_ERROR_NUM(FindNextFile, errCode,
                                  "Failed to search for next file in directory: %s", dirPath);
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
        ELOG_REPORT_SYS_ERROR(stat, "Failed to check file %s status: %d", path, errCode);
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
        ELOG_REPORT_SYS_ERROR(opendir, "Failed to open directory %s for reading: %d", dirPath,
                              errCode);
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
        ELOG_REPORT_SYS_ERROR(readdir, "Failed to list files in directory %s: %d", dirPath,
                              errCode);
        closedir(dirp);
        return false;
    }
    if (closedir(dirp) < 0) {
        ELOG_REPORT_SYS_ERROR(closedir, "Failed to terminate listing files in directory %s: %d",
                              dirPath, errCode);
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
      m_bytesLogged(0),
      m_currentSegment(nullptr),
      m_entered(0),
      m_left(0),
      m_currentlyOpeningSegment(0),
      m_segmentOpenerId(0) {
    // open current segment (start a new one if needed)
    setNativelyThreadSafe();
    setAddNewLine(true);
    openSegment();
}

ELogSegmentedFileTarget::~ELogSegmentedFileTarget() {}

bool ELogSegmentedFileTarget::startLogTarget() { return openSegment(); }

bool ELogSegmentedFileTarget::stopLogTarget() {
    if (m_currentSegment.load(std::memory_order_relaxed) != nullptr) {
        if (fclose(m_currentSegment) == -1) {
            ELOG_REPORT_SYS_ERROR(fopen, "Failed to close log segment");
            return false;
        }
        m_currentSegment = nullptr;
    }
    return true;
}

void ELogSegmentedFileTarget::logFormattedMsg(const std::string& formattedLogMsg) {
    // first thing, increment the entered count
    m_entered.fetch_add(1, std::memory_order_acquire);

    // check if segment switch is required
    // TODO: if message size exceeds segment size then this logic fails!
    uint32_t msgSizeBytes = formattedLogMsg.length();
    uint64_t bytesLogged = m_bytesLogged.fetch_add(msgSizeBytes, std::memory_order_relaxed);
    uint64_t prevSegmentId = bytesLogged / m_segmentLimitBytes;
    uint64_t currSegmentId = (bytesLogged + msgSizeBytes) / m_segmentLimitBytes;
    if (prevSegmentId != currSegmentId) {
        // crossed a segment boundary, so open a new segment
        // in the meantime other threads push to pending message queue until new segment is ready
        advanceSegment(currSegmentId, formattedLogMsg);
        m_left.fetch_add(1, std::memory_order_release);
        // NOTE: after segment is advanced the log message is already logged
        return;
    } else if (currSegmentId > m_segmentCount.load(std::memory_order_relaxed)) {
        // new segment is not ready yet, so push into pending queue
        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_pendingMsgQueue.push_front(formattedLogMsg);
        }
        // don't forget to increase left counter
        m_left.fetch_add(1, std::memory_order_release);
        return;
    }

    // NOTE: the following call to fputs() is guaranteed to be atomic according to POSIX
    FILE* currentSegment = m_currentSegment.load(std::memory_order_relaxed);
    if (fputs(formattedLogMsg.c_str(), currentSegment) == EOF) {
        ELOG_REPORT_SYS_ERROR(fputs, "Failed to write to log file");
        // TODO: in order to avoid log flooding, this error message must be emitted only once!
        // alternatively, the log target should be marked as unusable and reject all requests to log
        // messages. This is true for all log targets.
    }

    // we must remember the segment we used for logging, so that we can tell during flush it is
    // the same segment (so that if it changed, no flush will take place)
    m_usedSegment = currentSegment;

    // mark log finish
    m_left.fetch_add(1, std::memory_order_release);
}

void ELogSegmentedFileTarget::flushLogTarget() {
    // first thing, increment the entered count
    m_entered.fetch_add(1, std::memory_order_acquire);

    // we make sure segment is not just being replaced
    // we use the same logic as if logging a zero sized message
    uint64_t bytesLogged = m_bytesLogged.load(std::memory_order_relaxed);
    uint64_t segmentId = bytesLogged / m_segmentLimitBytes;
    if (segmentId == m_segmentCount.load(std::memory_order_relaxed)) {
        // we are safe and guarded to access the current segment because the entered count has been
        // incremented, and the current segment will not be closed until the left counter is
        // incremented as well
        FILE* currentSegment = m_currentSegment.load(std::memory_order_relaxed);
        if (m_usedSegment == currentSegment) {
            if (fflush(currentSegment) == EOF) {
                ELOG_REPORT_SYS_ERROR(fflush, "Failed to flush log file");
            }
        }
    } else {
        // a segment is right now being replaced, so the request can be discarded
    }

    // last thing, increment the left count
    m_left.fetch_add(1, std::memory_order_release);
}

bool ELogSegmentedFileTarget::openSegment() {
    // get segment count and last segment size
    uint32_t segmentCount = 0;
    uint32_t lastSegmentSizeBytes = 0;
    if (!getSegmentCount(segmentCount, lastSegmentSizeBytes)) {
        return false;
    }
    // NOTE: we don't need a real total value for bytes-logged, but rather just a reference point
    // from the start of the last segment
    m_segmentCount.store(segmentCount, std::memory_order_relaxed);
    m_bytesLogged.store(lastSegmentSizeBytes, std::memory_order_relaxed);

    // if last segment is too large, then start a new segment
    if (lastSegmentSizeBytes > m_segmentLimitBytes) {
        m_segmentCount.fetch_add(1, std::memory_order_relaxed);
        m_bytesLogged.store(0, std::memory_order_relaxed);
    }

    // open the segment file for appending
    std::string segmentPath;
    formatSegmentPath(segmentPath, m_segmentCount.load(std::memory_order_relaxed));
    FILE* segment = fopen(segmentPath.c_str(), "a");
    if (segment == nullptr) {
        int errCode = errno;
        ELOG_REPORT_SYS_ERROR(fopen, "Failed to open segment file %s: %d", segmentPath.c_str(),
                              errCode);
        return false;
    }
    m_currentSegment.store(segment, std::memory_order_relaxed);
    m_segmentOpenerId.store(m_segmentCount.load(std::memory_order_relaxed) + 1,
                            std::memory_order_relaxed);
    return true;
}

bool ELogSegmentedFileTarget::getSegmentCount(uint32_t& segmentCount,
                                              uint32_t& lastSegmentSizeBytes) {
    // scan directory for all files with matching name:
    // <log-path>/<log-name>.<log-id>.log
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
    ELOG_REPORT_TRACE("Max segment index %d from segment file %s", maxSegmentIndex,
                      lastSegmentName.c_str());

    lastSegmentSizeBytes = 0;
    if (maxSegmentIndex >= 0) {
        if (!getFileSize((m_logPath + "/" + lastSegmentName).c_str(), lastSegmentSizeBytes)) {
            return false;
        }
        ELOG_REPORT_TRACE("Last segment file size: %u", lastSegmentSizeBytes);
        segmentCount = maxSegmentIndex;
    } else {
        ELOG_REPORT_TRACE("No segments found, using segment index 0");
        segmentCount = 0;
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
    if (fileName.starts_with(m_logName) && fileName.ends_with(LOG_SUFFIX)) {
        // extract segment index
        // check for special case - segment zero has no index embedded
        if (fileName.compare(m_logName + LOG_SUFFIX) == 0) {
            // first segment with no index
            segmentIndex = 0;
            // otherwise we already have a segment with higher index
        } else {
            // shave off prefix and suffix (and another additional dot)
            if (fileName[m_logName.length()] != '.') {
                // unexpected file name
                ELOG_REPORT_ERROR("Invalid segment file name: %s", fileName.c_str());
                return false;
            }
            std::string segmentIndexStr =
                fileName.substr(m_logName.length() + 1,
                                fileName.length() - m_logName.length() - strlen(LOG_SUFFIX) - 1);
            try {
                std::size_t pos = 0;
                segmentIndex = std::stoi(segmentIndexStr, &pos);
                if (pos != segmentIndexStr.length()) {
                    // something is wrong, we have excess chars, so we ignore this segment
                    ELOG_REPORT_ERROR(
                        "Invalid segment file name, excess chars after segment index: %s",
                        fileName.c_str());
                    segmentIndex = -1;
                    return false;
                }
                ELOG_REPORT_TRACE("Found segment index %u from segment file %s", segmentIndex,
                                  fileName.c_str());
            } catch (std::exception& e) {
                ELOG_REPORT_SYS_ERROR(
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
        ELOG_REPORT_SYS_ERROR(std::filesystem::file_size, "Failed to get size of segment %s: %s",
                              filePath, e.what());
        return false;
    }
}

void ELogSegmentedFileTarget::formatSegmentPath(std::string& segmentPath, uint32_t segmentId) {
    std::stringstream s;
    s << m_logPath << "/" << m_logName;
    if (segmentId > 0) {
        s << "." << segmentId;
    }
    s << LOG_SUFFIX;
    segmentPath = s.str();
    ELOG_REPORT_TRACE("Using segment path %s", segmentPath.c_str());
}

bool ELogSegmentedFileTarget::advanceSegment(uint32_t segmentId, const std::string& logMsg) {
    // we need to:
    // - open new segment
    // - switch segments
    // - busy wait until previous segment loggers are finished
    // - log message
    int64_t openerCount = m_currentlyOpeningSegment.fetch_add(1, std::memory_order_relaxed) + 1;
    ELOG_REPORT_TRACE("Opening segment %u, current opener count: %" PRId64, segmentId, openerCount);
    FILE* prevSegment = m_currentSegment.load(std::memory_order_relaxed);
    std::string segmentPath;
    formatSegmentPath(segmentPath, segmentId);
    FILE* nextSegment = fopen(segmentPath.c_str(), "a");
    if (nextSegment == nullptr) {
        ELOG_REPORT_SYS_ERROR(fopen, "Failed to open segment file %s", segmentPath.c_str());
        m_currentlyOpeningSegment.fetch_add(-1, std::memory_order_relaxed);
        return false;
    }

    // since there are many that can open a new segment in parallel, we need to wait for our turn
    // NOTE: we cannot wait for segment-count, because that would cause race when pulling messages
    // from the pending queue (each segment opener advances the segment-count variable to let
    // logging threads know that a segment has been opened and can be used)
    ELOG_REPORT_TRACE("Current segment opener id: %" PRIu64,
                      m_segmentOpenerId.load(std::memory_order_relaxed));
    // NOTE: point of serialization, only one thread at a time can pass (i.e. lock-free but not
    // wait-free)
    while (m_segmentOpenerId.load(std::memory_order_relaxed) != segmentId) {
        std::this_thread::yield();
    }
    ELOG_REPORT_TRACE("Segment opener %" PRIu64 " advancing to switch segment and drain queue",
                      segmentId);

    // first write this thread's log message
    // NOTE: we write to the previous segment
    if (fputs(logMsg.c_str(), prevSegment) == EOF) {
        ELOG_REPORT_SYS_ERROR(fputs, "Failed to write to log file");
    }

    // it is theoretically possible that during log flooding we will not have time to close one
    // segment, when another segment already needs to be opened. this is ok, as several threads
    // will get "stuck" for a short while, each trying to close its segment

    // switch segments (allow other threads start writing to next segment while this thread writes
    // messages to previous segment)
    if (!m_currentSegment.compare_exchange_strong(prevSegment, nextSegment,
                                                  std::memory_order_relaxed)) {
        ELOG_REPORT_ERROR("Failed to switch log segment files, suspected log flooding");
        return false;
    }

    // let others know the new segment is ready for writing, so they can stop pushing messages to
    // the pending queue. from this point onward message order DOES NOT get intermixed, because new
    // logging threads write log messages to the new segment, whereas pending messages are written
    // to the previous segment. as a result, previous segment size may get breached, but this is
    // inevitable in case of log flooding.
    ELOG_REPORT_TRACE("Opening new segment %" PRIu64 " for writing", segmentId);
    m_segmentCount.fetch_add(1, std::memory_order_relaxed);

    // now we need to wait until all current users of the previous segment are done
    // we start measuring from this point on, so we are on the safe side (we might actually wait a
    // bit more than required, but that is ok)
    uint64_t entered = m_entered.load(std::memory_order_relaxed);
    ELOG_REPORT_TRACE("Entered count: %" PRIu64, entered);

    // NOTE: other threads may also be opening a segment in parallel, each of which has incremented
    // the 'entered' counter, so we need to add this number (the current amount of segment opener
    // threads), otherwise we will never reach equality here, but when we see that "entered == left
    // + currently-opening-segment" we can tell all other threads that are not opening a segment
    // have already left (i.e. "simple" loggers), and the new ones that come will use the open
    // segment (or keep pushing to queue if more than one thread opens a new a segment, but in that
    // case entered will get a higher number so this thread will get out of the while loop)
    uint64_t yieldCount = 0;
    while (entered > m_left.load(std::memory_order_relaxed) +
                         m_currentlyOpeningSegment.load(std::memory_order_relaxed)) {
        // check for more queued items
        std::list<std::string> logMsgs;
        {
            // drain to a temp queue and write file outside lock scope to minimize lock waiting time
            // of other threads
            std::unique_lock<std::mutex> lock(m_lock);
            while (!m_pendingMsgQueue.empty()) {
                const std::string& logMsg = m_pendingMsgQueue.back();
                logMsgs.push_front(logMsg);
                m_pendingMsgQueue.pop_back();
            }
        }
        if (logMsgs.empty()) {
            std::this_thread::yield();
            if (++yieldCount == 10000) {
                ELOG_REPORT_TRACE("Stuck: entered = %" PRIu64 ", left = %" PRIu64
                                  ", currently opening segment = %" PRId64,
                                  entered, m_left.load(std::memory_order_relaxed),
                                  m_currentlyOpeningSegment.load(std::memory_order_relaxed));
            }
        } else {
            // NOTE: we are logging to the previous segment, so that we keep order of messages. this
            // may cause slight bloating of the segment, but that is probably acceptable in a
            // lock-free solution
            logMsgQueue(logMsgs, prevSegment);
        }
    }

    // log the last batch, there shouldn't be any more
    {
        // drain to a temp queue and write file outside lock scope to minimize lock waiting time
        // of other threads
        std::unique_lock<std::mutex> lock(m_lock);
        ELOG_REPORT_TRACE("Logging %u final pending messages", m_pendingMsgQueue.size());
        logMsgQueue(m_pendingMsgQueue, prevSegment);
    }

    // now we can let the next segment openers to advance
    m_segmentOpenerId.fetch_add(1, std::memory_order_relaxed);

    // let other segment opening threads that we are done
    m_currentlyOpeningSegment.fetch_add(-1, std::memory_order_relaxed);

    // NOTE: only now we can close the segment (and this should also auto-flush)
    if (fclose(prevSegment) == -1) {
        ELOG_REPORT_SYS_ERROR(fclose, "Failed to close segment log file");
        return false;
    }
    return true;
}

void ELogSegmentedFileTarget::logMsgQueue(std::list<std::string>& logMsgs, FILE* segmentFile) {
    ELOG_REPORT_TRACE("Logging %u pending messages", logMsgs.size());
    while (!logMsgs.empty()) {
        const std::string& logMsg = logMsgs.back();
        if (fputs(logMsg.c_str(), segmentFile) == EOF) {
            ELOG_REPORT_SYS_ERROR(fputs, "Failed to write to log file");
        }
        logMsgs.pop_back();
    }
}

}  // namespace elog
