#include "elog_file_target.h"

#include "elog_def.h"
#ifdef ELOG_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#include "elog_error.h"

namespace elog {

#ifdef ELOG_WINDOWS
static uint32_t getOptimalBlockSize(FILE* fileHandle) {
    // get file descriptor
    // (on Windows fileno() is deprecated, _fileno() should be used instead)
    int fd = _fileno(fileHandle);
    HANDLE win32Handle = (HANDLE)_get_osfhandle(fd);
    if (win32Handle == INVALID_HANDLE_VALUE) {
        ELOG_REPORT_WIN32_ERROR(_get_osfhandle,
                                "Failed to get file handle for inquiring optimal block size");
        return 0;
    }
    FILE_STORAGE_INFO info = {};
    if (!GetFileInformationByHandleEx(win32Handle, FileStorageInfo, (&info),
                                      sizeof(FILE_STORAGE_INFO))) {
        ELOG_REPORT_WIN32_ERROR(
            GetFileInformationByHandleEx,
            "Failed to get file storage information for inquiring optimal block size");
        return 0;
    }
    return info.PhysicalBytesPerSectorForPerformance;
}
#else
static uint32_t getOptimalBlockSize(FILE* fileHandle) {
    struct stat stats;
    if (fstat(fileno(fileHandle), &stats) == -1)  // POSIX only
    {
        ELOG_REPORT_SYS_ERROR(fstat, "Failed to get file status for buffer size resetting");
        return 0;
    }
    return stats.st_blksize;
}
#endif

ELogFileTarget::ELogFileTarget(const char* filePath, ELogFlushPolicy* flushPolicy /* = nullptr */)
    : ELogTarget("file", flushPolicy),
      m_filePath(filePath),
      m_fileHandle(nullptr),
      m_shouldClose(false) {
    setNativelyThreadSafe();
    setAddNewLine(true);
}

bool ELogFileTarget::configureOptimalBufferSize() {
    // use optimal buffer size recommended by underlying OS
    uint32_t optimalBlockSize = getOptimalBlockSize(m_fileHandle);
    if (optimalBlockSize == 0) {
        fclose(m_fileHandle);
        m_fileHandle = nullptr;
        return false;
    }
    ELOG_REPORT_TRACE("Recommended buffer size is %u, but optimal block size is %ld\n",
                      (unsigned)BUFSIZ, optimalBlockSize);
    fprintf(stderr, "Recommended buffer size is %u, but optimal block size is %u\n",
            (unsigned)BUFSIZ, optimalBlockSize);

    if (setvbuf(m_fileHandle, NULL, _IOFBF, optimalBlockSize) != 0) {
        ELOG_REPORT_SYS_ERROR(setvbuf,
                              "Failed to configure log file %s buffer size to recommended size %u",
                              m_filePath.c_str(), optimalBlockSize);
        fclose(m_fileHandle);
        m_fileHandle = nullptr;
        return false;
    }
    return true;
}

bool ELogFileTarget::startLogTarget() {
    if (m_fileHandle == nullptr) {
        m_fileHandle = fopen(m_filePath.c_str(), "a");
        if (m_fileHandle == nullptr) {
            ELOG_REPORT_SYS_ERROR(fopen, "Failed to open log file %s", m_filePath.c_str());
            return false;
        }
        m_shouldClose = true;
    }
#if 0
    if (!configureOptimalBufferSize()) {
        ELOG_REPORT_ERROR("Failed to configure optimal buffer size on Windows/MSVC");
        fclose(m_fileHandle);
        m_fileHandle = nullptr;
        return false;
    }
#endif
    return true;
}

bool ELogFileTarget::stopLogTarget() {
    if (m_fileHandle != nullptr && m_shouldClose) {
        flush();
        if (fclose(m_fileHandle) == -1) {
            ELOG_REPORT_SYS_ERROR(fclose, "Failed to close log file %s", m_filePath.c_str());
            return false;
        }
    }
    return true;
}

void ELogFileTarget::logFormattedMsg(const std::string& formattedLogMsg) {
    // NOTE: according to https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_concurrency.html
    // gcc documentations states that: "POSIX standard requires that C stdio FILE* operations are
    // atomic. POSIX-conforming C libraries (e.g, on Solaris and GNU/Linux) have an internal mutex
    // to serialize operations on FILE*s."
    // therefore no lock is required here
    fputs(formattedLogMsg.c_str(), m_fileHandle);
}

void ELogFileTarget::flushLogTarget() {
    if (fflush(m_fileHandle) == EOF) {
        ELOG_REPORT_SYS_ERROR(fflush, "Failed to flush log file");
    }
}

}  // namespace elog
