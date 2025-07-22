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

#include "elog_common.h"
#include "elog_error.h"

// TODO: need hard coded limits for all configuration values, and from there we can derive required
// type. in addition, load functions with limits should be written to check limits

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
static const char* LOG_SUFFIX = ".log";

#define ELOG_SEGMENT_EPOCH_RING_SIZE 4096

#define MEGA_BYTE (1024 * 1024)

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
        ELOG_REPORT_WIN32_ERROR_NUM(FindNextFile, errCode,
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

bool ELogSegmentedFileTarget::SegmentData::open(const char* segmentPath,
                                                uint64_t fileBufferSizeBytes /* = 0 */,
                                                bool useLock /* = true */,
                                                bool truncateSegment /* = false */) {
    m_segmentFile = elog_fopen(segmentPath, truncateSegment ? "w" : "a");
    if (m_segmentFile == nullptr) {
        int errCode = errno;
        ELOG_REPORT_SYS_ERROR(fopen, "Failed to open segment file %s: %d", segmentPath, errCode);
        return false;
    }

    // use buffered file if configured to do so
    if (fileBufferSizeBytes > 0) {
        m_bufferedFileWriter =
            new (std::nothrow) ELogBufferedFileWriter(fileBufferSizeBytes, useLock);
        if (m_bufferedFileWriter == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to allocate buffered writer during log segment initialization, out of "
                "memory");
            fclose(m_segmentFile);
            m_segmentFile = nullptr;
            return false;
        } else {
            m_bufferedFileWriter->setFileHandle(m_segmentFile);
        }
    }
    return true;
}

bool ELogSegmentedFileTarget::SegmentData::log(const char* logMsg, size_t len) {
    // NOTE: we do not log error message to avoid log flooding
    // TODO: we can optimize here, since each logging thread can grab its own section of the buffer,
    // and the one the causes overflow should wait for buffer flush, this will avoid using a lock,
    // but then again, the flushing thread will have to wait for all previous logging threads to
    // finish memcpy (can be achieved with GC/epoch), and then all that came after the overlowing
    // thread, will have to wait too for buffer flush. Does this complexity worth the effort? Seems
    // that gain is marginal due to constant waiting for buffer flush
    if (m_bufferedFileWriter != nullptr) {
        return m_bufferedFileWriter->logMsg(logMsg, len);
    } else {
        // NOTE: the following call to fputs() is guaranteed to be atomic according to POSIX
        return (fputs(logMsg, m_segmentFile) != EOF);
    }
}

bool ELogSegmentedFileTarget::SegmentData::drain() {
    bool res = true;
    while (!m_pendingMsgs.empty()) {
        const std::string& logMsg = m_pendingMsgs.back();
        if (!log(logMsg.c_str(), logMsg.length())) {
            // NOTE: do not log error message to avoid log flooding (can consider some
            // attenuation/aggregation - log 1 error message within X time)
            // ELOG_REPORT_ERROR("Failed to write to segment log file");
            // TODO: we can add this to some statistics object (log-error-count)
            res = false;
        }
        m_pendingMsgs.pop();
    }
    return res;
}

bool ELogSegmentedFileTarget::SegmentData::flush() {
    if (m_bufferedFileWriter != nullptr) {
        if (!m_bufferedFileWriter->flushLogBuffer()) {
            ELOG_REPORT_ERROR("Failed to flush buffered writer");
            return false;
        }
    }
    if (fflush(m_segmentFile) == EOF) {
        ELOG_REPORT_SYS_ERROR(fflush, "Failed to flush log file");
        return false;
    }
    return true;
}

bool ELogSegmentedFileTarget::SegmentData::close() {
    // drain pending messages first
    if (!drain()) {
        return false;
    }

    // flush buffered writer (if any), then flush file
    if (!flush()) {
        return false;
    }

    // now close file
    if (fclose(m_segmentFile) == -1) {
        ELOG_REPORT_SYS_ERROR(fclose, "Failed to close segment log file");
    }

    // cleanup members
    if (m_bufferedFileWriter != nullptr) {
        delete m_bufferedFileWriter;
        m_bufferedFileWriter = nullptr;
    }
    m_segmentFile = nullptr;
    return true;
}

ELogSegmentedFileTarget::ELogSegmentedFileTarget(
    const char* logPath, const char* logName, uint64_t segmentLimitBytes,
    uint32_t segmentRingSize /* = ELOG_DEFAULT_SEGMENT_RING_SIZE */,
    uint64_t fileBufferSizeBytes /* = 0 */, uint32_t segmentCount /* = 0 */,
    ELogFlushPolicy* flushPolicy /* = nullptr */)
    : ELogTarget("segmented-file", flushPolicy),
      m_segmentLimitBytes(segmentLimitBytes),
      m_fileBufferSizeBytes(fileBufferSizeBytes),
      m_segmentRingSize(segmentRingSize),
      m_segmentCount(segmentCount),
      m_currentSegment(nullptr),
      m_epoch(0),
      m_logPath(logPath),
      m_logName(logName) {
    // check for limits
    if (segmentLimitBytes > ELOG_MAX_SEGMENT_LIMIT_BYTES) {
        ELOG_REPORT_WARN("Truncating segment size limit from %" PRIu64 " bytes to %" PRIu64
                         " bytes (exceeding allowed limit), at "
                         "segmented/rotating log target at %s",
                         segmentLimitBytes, ELOG_MAX_SEGMENT_LIMIT_BYTES);
        m_segmentLimitBytes = ELOG_MAX_SEGMENT_LIMIT_BYTES * 1024 * 1024;
    }
    if (m_segmentRingSize > ELOG_MAX_SEGMENT_RING_SIZE) {
        ELOG_REPORT_WARN(
            "Truncating segment ring size from %u to %u pending log messages (exceeding allowed "
            "limit), at segmented/rotating log target at %s",
            m_segmentRingSize, (unsigned)ELOG_MAX_SEGMENT_RING_SIZE);
        m_segmentRingSize = ELOG_MAX_SEGMENT_RING_SIZE;
    }
    if (m_segmentCount > ELOG_MAX_SEGMENT_COUNT) {
        ELOG_REPORT_WARN(
            "Truncating segment count from %u to %u (exceeding allowed limit), at rotating log "
            "target at %s",
            m_segmentCount, (unsigned)ELOG_MAX_SEGMENT_COUNT);
        m_segmentCount = ELOG_MAX_SEGMENT_COUNT;
    }

    // set defaults
    if (m_segmentRingSize == 0) {
        m_segmentRingSize = ELOG_DEFAULT_SEGMENT_RING_SIZE;
        ELOG_REPORT_TRACE(
            "Using default segment ring size %u at segmented/rotating log target at %s",
            m_segmentRingSize, logName);
    }

    setNativelyThreadSafe();
    setAddNewLine(true);
}

ELogSegmentedFileTarget::~ELogSegmentedFileTarget() {}

bool ELogSegmentedFileTarget::startLogTarget() {
    m_epochSet.resizeRing(ELOG_SEGMENT_EPOCH_RING_SIZE);
    return openSegment();
}

bool ELogSegmentedFileTarget::stopLogTarget() {
    SegmentData* segmentData = m_currentSegment.load(std::memory_order_acquire);
    if (segmentData != nullptr) {
        // NOTE: the following call logs all pending messages, although there shouldn't be any,
        // since if all threads stopped, then even during segment switch, the thread doing the
        // switch will drain all pending messages before returning - but this is just for safety
        if (!segmentData->close()) {
            ELOG_REPORT_ERROR("Failed to close log segment");
            return false;
        }

        // NOTE: it is expected that at this point no thread is trying to log messages anymore
        // this is the user's responsibility
        delete segmentData;
        m_currentSegment.store(nullptr, std::memory_order_release);
    }
    return true;
}

void ELogSegmentedFileTarget::logFormattedMsg(const char* formattedLogMsg, size_t length) {
    // first thing, increment the epoch
    uint64_t epoch = m_epoch.fetch_add(1, std::memory_order_acquire);

    // check if segment switch is required
    // NOTE: if message size exceeds segment size then the current segment will be larger than
    // configured limit, but we are OK with that
    SegmentData* segmentData = m_currentSegment.load(std::memory_order_acquire);
    uint64_t msgSizeBytes = length;
    uint64_t bytesLogged =
        segmentData->m_bytesLogged.fetch_add(msgSizeBytes, std::memory_order_relaxed);
    if (bytesLogged <= m_segmentLimitBytes && (bytesLogged + msgSizeBytes) > m_segmentLimitBytes) {
        // crossed a segment boundary, so open a new segment
        // in the meantime other threads push to pending message queue until new segment is ready
        advanceSegment(segmentData->m_segmentId + 1, formattedLogMsg, epoch);
        // NOTE: current thread's epoch is already closed by call to advanceSegment()
        // NOTE: after segment is advanced the log message is already logged
        return;
    } else if (bytesLogged > m_segmentLimitBytes) {
        // new segment is not ready yet, so push into pending queue
        segmentData->m_pendingMsgs.push(formattedLogMsg);
        // don't forget to mark transaction epoch end
        m_epochSet.insert(epoch);
        return;
    }

    // write data to current log segment
    if (!segmentData->log(formattedLogMsg, length)) {
        // TODO: in order to avoid log flooding, this error message must be emitted only once!
        // alternatively, the log target should be marked as unusable and reject all requests to log
        // messages. This is true for all log targets.
        // for the time being we avoid emitting this error message
        // ELOG_REPORT_ERROR("Failed to write to segment log file");
    }

    // mark log finish
    m_epochSet.insert(epoch);
}

void ELogSegmentedFileTarget::flushLogTarget() {
    // first thing, increment the epoch count
    uint64_t epoch = m_epoch.fetch_add(1, std::memory_order_acquire);

    // we make sure segment is not just being replaced
    // we use the same logic as if logging a zero sized message
    SegmentData* segmentData = m_currentSegment.load(std::memory_order_relaxed);
    uint64_t bytesLogged = segmentData->m_bytesLogged.load(std::memory_order_relaxed);
    if (bytesLogged < m_segmentLimitBytes) {
        // we are safe because epoch was taken BEFORE current segment pointer, even if segment is
        // now being replaced
        // NOTE: no pending message draining takes place here, since this is external periodic flush
        if (!segmentData->flush()) {
            ELOG_REPORT_ERROR("Failed to flush segment log file");
        }
    } else {
        // a segment is right now being replaced, so the request can be discarded
    }

    // last thing, mark transaction end
    m_epochSet.insert(epoch);
}

bool ELogSegmentedFileTarget::openSegment() {
    // get segment count and last segment size
    uint32_t segmentCount = 0;
    uint64_t lastSegmentSizeBytes = 0;
    if (!getSegmentCount(segmentCount, lastSegmentSizeBytes)) {
        return false;
    }

    if (m_segmentCount > 0 && segmentCount >= m_segmentCount) {
        ELOG_REPORT_ERROR(
            "Cannot initialize rotating log, found in log folder too many log segments (expecting "
            "maximum: %" PRIu64 ", found: %u), aborting",
            m_segmentCount, segmentCount);
        return false;
    }

    // if last segment is too large, then start a new segment
    if (lastSegmentSizeBytes > m_segmentLimitBytes) {
        ++segmentCount;
        lastSegmentSizeBytes = 0;
    }

    // create segment data object
    SegmentData* segmentData = new (std::nothrow) SegmentData(segmentCount, lastSegmentSizeBytes);
    if (segmentData == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate segment object, out of memory");
        return false;
    }
    if (!segmentData->m_pendingMsgs.initialize(m_segmentRingSize)) {
        ELOG_REPORT_ERROR("Failed to initialize ring buffer of segment object");
        delete segmentData;
        return false;
    }

    // open the segment file for appending
    std::string segmentPath;
    formatSegmentPath(segmentPath, segmentCount);
    bool useLock = !isExternallyThreadSafe();
    bool truncateSegment = (m_segmentCount > 0);
    if (!segmentData->open(segmentPath.c_str(), m_fileBufferSizeBytes, useLock, truncateSegment)) {
        delete segmentData;
        return false;
    }
    m_currentSegment.store(segmentData, std::memory_order_relaxed);
    return true;
}

bool ELogSegmentedFileTarget::getSegmentCount(uint32_t& segmentCount,
                                              uint64_t& lastSegmentSizeBytes) {
    // scan directory for all files with matching name:
    // <log-path>/<log-name>.<log-id>.log
    std::vector<std::string> fileNames;
    if (!scanDirFiles(m_logPath.c_str(), fileNames)) {
        return false;
    }

    bool segmentFound = false;  // init with "no segments" value
    uint32_t maxSegmentIndex = 0;
    std::string lastSegmentName;
    std::vector<std::string>::iterator itr = fileNames.begin();
    while (itr != fileNames.end()) {
        const std::string fileName = *itr;
        uint32_t segmentIndex = 0;
        // NOTE: if we failed to extract segment id from file - that is OK, since the directory
        // might contain log segmented with different name scheme
        if (getSegmentIndex(fileName, segmentIndex)) {
            segmentFound = true;
            if (maxSegmentIndex < segmentIndex) {
                maxSegmentIndex = segmentIndex;
                lastSegmentName = fileName;
            }
        }
        ++itr;
    }

    lastSegmentSizeBytes = 0;
    if (segmentFound) {
        ELOG_REPORT_TRACE("Max segment index %u from segment file %s", maxSegmentIndex,
                          lastSegmentName.c_str());
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

bool ELogSegmentedFileTarget::getSegmentIndex(const std::string& fileName, uint32_t& segmentIndex) {
    if (fileName.starts_with(m_logName) && fileName.ends_with(LOG_SUFFIX)) {
        // extract segment index
        // check for special case - segment zero has no index embedded
        if (fileName.compare(m_logName + LOG_SUFFIX) == 0) {
            // first segment with no index
            segmentIndex = 0;
            return true;
        }

        // shave off prefix and suffix (and another additional dot)
        if (fileName[m_logName.length()] != '.') {
            // unexpected file name
            ELOG_REPORT_WARN("Invalid segment file name: %s", fileName.c_str());
            return false;
        }
        std::string segmentIndexStr =
            fileName.substr(m_logName.length() + 1,
                            fileName.length() - m_logName.length() - strlen(LOG_SUFFIX) - 1);
        try {
            std::size_t pos = 0;
            segmentIndex = std::stoul(segmentIndexStr, &pos);
            if (pos != segmentIndexStr.length()) {
                // something is wrong, we have excess chars, so we ignore this segment
                ELOG_REPORT_ERROR("Invalid segment file name, excess chars after segment index: %s",
                                  fileName.c_str());
                return false;
            }
            ELOG_REPORT_TRACE("Found segment index %u from segment file %s", segmentIndex,
                              fileName.c_str());
            return true;
        } catch (std::exception& e) {
            ELOG_REPORT_SYS_ERROR(
                std::stoi, "Invalid segment file name %s, segment index could not be parsed: %s",
                fileName.c_str(), e.what());
            return false;
        }
    }
    return false;
}

bool ELogSegmentedFileTarget::getFileSize(const char* filePath, uint64_t& fileSize) {
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

bool ELogSegmentedFileTarget::advanceSegment(uint32_t segmentId, const std::string& logMsg,
                                             uint64_t currentEpoch) {
    // quickly open a new segment, then switch segments

    // NOTE: we could have used a GC here, and retire the segment object, having its destructor log
    // all pending messages and close the segment. although this looks like a nice idea, in reality
    // it has a few major drawbacks:
    //
    // 1. a random thread would be the victim and would pay the price for logging pending messages.
    // this is more complex then simply having the segment switching thread wait and do it,
    // especially when needing to debug. Complexity can be suffered only when the added value is
    // worth it.
    //
    // 2. Without tight waiting by the segment switching thread, the segment may be recycled not as
    // soon as possible, and this depends on the GC frequency and the number of logging threads.
    // This will first cause the segment file to be left open for more time. in case of imminent
    // crash we prefer log files to contain data as soon as possible (that is the whole idea of
    // logging, right?) - BTW, it is planned in the future to add "emergency dump" feature, such
    // that during crash/core-dump, we catch the signal, and dump all log buffers in all threads
    // into an emergency log file (we can do that since all log buffers are accessible from loggers
    // through log sources).
    //
    // 3. During this delayed segment closing period, the pending messages ring buffer gets filled.
    // if it becomes full, then logging threads will get stuck. In fact we may end up in a deadlock
    // here, since logging threads increment the epoch, and if they get stuck the epoch will get
    // stuck, and the GC will not kick in, and the segment will not be recycled, meaning that the
    // ring buffer of the segment will remain full, because pending messages will not be pulled
    // (according to this design they get pulled during segment recycling).

    // So, considering all this, it is decided to have the segment switching thread wait for epoch
    // to advance, while evicting occasionally the pending messages queue.

    // when rotating segment id should be cycling
    if (m_segmentCount > 0) {
        segmentId = segmentId % m_segmentCount;
    }

    // open a new segment
    SegmentData* nextSegment = new (std::nothrow) SegmentData(segmentId);
    if (nextSegment == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate segment data for segment %u, out of memory",
                          segmentId);
        return false;
    }
    if (!nextSegment->m_pendingMsgs.initialize(m_segmentRingSize)) {
        ELOG_REPORT_ERROR("Failed to initialize ring buffer of segment object");
        delete nextSegment;
        return false;
    }

    // create segment file
    std::string segmentPath;
    formatSegmentPath(segmentPath, segmentId);
    bool useLock = !isExternallyThreadSafe();
    bool truncateSegment = (m_segmentCount > 0);
    if (!nextSegment->open(segmentPath.c_str(), m_fileBufferSizeBytes, useLock, truncateSegment)) {
        delete nextSegment;
        return false;
    }

    // switch segments
    SegmentData* prevSegment = m_currentSegment.load(std::memory_order_acquire);
    if (!m_currentSegment.compare_exchange_strong(prevSegment, nextSegment,
                                                  std::memory_order_seq_cst)) {
        ELOG_REPORT_ERROR("Failed to exchange segments, aborting");
        delete nextSegment;
        return false;
    }

    // from this point onward, loggers use new segment, so now we can retire segment to GC or wait
    // for epoch. the simpler option is to retire to GC.

    // first write this thread's log message
    // NOTE: we write to the previous segment
    if (!prevSegment->log(logMsg.c_str(), logMsg.length())) {
        // NOTE: this will not cause log flooding
        ELOG_REPORT_ERROR("Failed to write to segment log file");
        // nevertheless continue
    }

    // now we need to wait until all current users of the previous segment are done
    // we start measuring from this point on, so we are on the safe side (we might actually wait a
    // bit more than required, but that is ok)
    // NOTE: this is essentially the minimum active epoch problem, so we reuse the rolling bit set
    // from the private GC (see elog_gc.h/cpp)
    uint64_t targetEpoch = m_epoch.load(std::memory_order_relaxed);
    ELOG_REPORT_TRACE("Target epoch: %" PRIu64, targetEpoch);

    // NOTE: as we wait for minimum active epoch to advance, the current thread's epoch (obtained at
    // the beginning of logFormattedMsg()) is not closed, so the minimum active epoch will NEVER
    // advance beyond the current thread's epoch, which is ALWAYS smaller than the target epoch
    // (unless this is a single threaded scenario). For this reason we MUST close the current
    // thread's epoch to allow it to advance to the target epoch. We can do so safely at this early
    // stage, because it does not affect other logging threads in any way.
    m_epochSet.insert(currentEpoch);

    // as we wait for the epoch to advance, we keep moving queued messages to a local private queue
    // NOTE: the target epoch is the epoch to be given to the next transaction, so we check for
    // strictly lower minimum active epoch (which is actually number of finished transactions)
    uint64_t yieldCount = 0;
    while (targetEpoch > m_epochSet.queryFullPrefix()) {
        // check for more queued items
        std::list<std::string> logMsgs;
        {
            // drain to a temp queue and write file outside lock scope to minimize lock waiting time
            // of other threads
            // std::unique_lock<std::mutex> lock(prevSegment->m_lock);
            while (!prevSegment->m_pendingMsgs.empty()) {
                const std::string& pendingMsg = prevSegment->m_pendingMsgs.back();
                logMsgs.push_front(pendingMsg);
                prevSegment->m_pendingMsgs.pop();
            }
        }
        if (logMsgs.empty()) {
            std::this_thread::yield();
            if (++yieldCount == 10000) {
                ELOG_REPORT_TRACE("Stuck: target epoch = %" PRIu64 ", min active epoch = %" PRIu64,
                                  targetEpoch, m_epochSet.queryFullPrefix());
            }
        } else {
            // NOTE: we are logging to the previous segment, so that we keep order of messages. this
            // may cause slight bloating of the segment, but that is probably acceptable in a
            // lock-free solution
            logMsgQueue(logMsgs, prevSegment->m_segmentFile);
        }
    }

    // NOTE: from this point onward there should be no more incoming pending messages, as the epoch
    // has advanced beyond the reference epoch point above

    // NOTE: only now we can close the segment (and this should also auto-drain/flush)
    ELOG_REPORT_TRACE("Logging %u final pending messages", prevSegment->m_pendingMsgs.size());
    if (!prevSegment->close()) {
        ELOG_REPORT_ERROR("Failed to close segment log file");
        // nevertheless continue
    }

    delete prevSegment;
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
