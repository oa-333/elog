#include "elog_file_target.h"

#include "elog_def.h"
#ifdef ELOG_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#else
#include <stdio.h>
#include <sys/stat.h>
#endif

#include "elog_common.h"
#include "elog_report.h"

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

ELogFileTarget::ELogFileTarget(FILE* fileHandle, ELogFlushPolicy* flushPolicy /* = nullptr */,
                               bool shouldClose /* = false */)
    : ELogTarget("file", flushPolicy), m_fileHandle(fileHandle), m_shouldClose(shouldClose) {
    if (fileHandle == stderr) {
        setName("stderr");
    } else if (fileHandle == stdout) {
        setName("stdout");
    }
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
        m_fileHandle = elog_fopen(m_filePath.c_str(), "a");
        if (m_fileHandle == nullptr) {
            ELOG_REPORT_ERROR("Failed to open log file %s", m_filePath.c_str());
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
        // no need to flush, parent class already takes care of that
        if (fclose(m_fileHandle) == -1) {
            ELOG_REPORT_SYS_ERROR(fclose, "Failed to close log file %s", m_filePath.c_str());
            return false;
        }
    }
    return true;
}

void ELogFileTarget::logFormattedMsg(const char* formattedLogMsg, size_t length) {
    // NOTE: according to https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_concurrency.html
    // gcc documentations states that: "POSIX standard requires that C stdio FILE* operations are
    // atomic. POSIX-conforming C libraries (e.g, on Solaris and GNU/Linux) have an internal mutex
    // to serialize operations on FILE*s."
    // therefore no lock is required here when NOT running in thread-safe conditions
    if (isExternallyThreadSafe()) {
        // unlocked stdio is available only if _GNU_SOURCE is defined, regardless of Unix/Linux
        // platform/flavor
#ifdef _GNU_SOURCE
        fputs_unlocked(formattedLogMsg, m_fileHandle);
#else
        // NOTE: On Windows/MinGW platforms we do not have the stdio unlocked API, and implementing
        // it here directly (through the file descriptor) will bypass internal buffering offered by
        // fputs. Therefore on these platforms it is advised to use buffered file target instead.
        // NOTE: there is nothing we can do if the call fails, since reporting it would cause
        // flooding of error messages. Instead statistics can be maintained, and/or object state
        // TODO: add ticket/feature-request for statistics
        fputs(formattedLogMsg, m_fileHandle);
#endif
    } else {
        fputs(formattedLogMsg, m_fileHandle);
    }
}

void ELogFileTarget::flushLogTarget() {
    if (fflush(m_fileHandle) == EOF) {
        ELOG_REPORT_SYS_ERROR(fflush, "Failed to flush log file");
    }
}

}  // namespace elog
