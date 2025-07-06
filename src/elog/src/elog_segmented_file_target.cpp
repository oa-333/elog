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
static const char* LOG_SUFFIX = ".log";

#define ELOG_SEGMENTED_FILE_RING_SIZE 4096
#define ELOG_SEGMENT_PENDING_RING_SIZE (1024 * 1024)

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
      m_currentSegment(nullptr),
      m_epoch(0) {
    // open current segment (start a new one if needed)
    setNativelyThreadSafe();
    setAddNewLine(true);
}

ELogSegmentedFileTarget::~ELogSegmentedFileTarget() {}

bool ELogSegmentedFileTarget::startLogTarget() {
    m_epochSet.resizeRing(ELOG_SEGMENTED_FILE_RING_SIZE);
    return openSegment();
}

bool ELogSegmentedFileTarget::stopLogTarget() {
    // TODO: log pending messages
    SegmentData* segmentData = m_currentSegment.load(std::memory_order_acquire);
    if (segmentData != nullptr) {
        if (fclose(segmentData->m_segmentFile) == -1) {
            ELOG_REPORT_SYS_ERROR(fopen, "Failed to close log segment");
            return false;
        }
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
        {
            // TODO: refactor concurrent ring buffer from quantum log target, make it a template and
            // reuse in both cases. here we need a pair fo log msg and segment id. the thread
            // advancing segment should pull messages from the ring buffer and write to segment
            // until epoch advances. if the buffer indicates that a message from a new segment is
            // there, then it should not be pulled out (so we need peek() method), and the wait for
            // target epoch can end, because we know the pending messages for the segment are done.
            // BUT THIS IS WRONG
            // pending messages from different segments may be mixed due to unfortunate timing.
            // how do we fix that? should we open queue per segment? so a segment now is a small
            // object instead of FILE* pointer. it has file pointer, pending message queue, segment
            // id. we can put all active segments somewhere - again concurrent ring buffer?
            // with this solution, we only need refactor concurrent ring buffer, no need for peek.
            // also maximum concurrent segment count is not that high, so it is probably ok.
            // the queue per segment is again implemented as concurrent ring buffer, and here we
            // might need a larger ring buffer size. how much?
            // std::unique_lock<std::mutex> lock(segmentData->m_lock);
            segmentData->m_pendingMsgs.push(formattedLogMsg);
        }
        // don't forget to mark transaction epoch end
        m_epochSet.insert(epoch);
        return;
    }

    // NOTE: the following call to fputs() is guaranteed to be atomic according to POSIX
    if (fputs(formattedLogMsg, segmentData->m_segmentFile) == EOF) {
        ELOG_REPORT_SYS_ERROR(fputs, "Failed to write to log file");
        // TODO: in order to avoid log flooding, this error message must be emitted only once!
        // alternatively, the log target should be marked as unusable and reject all requests to log
        // messages. This is true for all log targets.
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
        if (fflush(segmentData->m_segmentFile) == EOF) {
            ELOG_REPORT_SYS_ERROR(fflush, "Failed to flush log file");
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
    uint32_t lastSegmentSizeBytes = 0;
    if (!getSegmentCount(segmentCount, lastSegmentSizeBytes)) {
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
    if (!segmentData->m_pendingMsgs.initialize(ELOG_SEGMENT_PENDING_RING_SIZE)) {
        ELOG_REPORT_ERROR("Failed to initialize ring buffer of segment object");
        delete segmentData;
        return false;
    }

    // open the segment file for appending
    std::string segmentPath;
    formatSegmentPath(segmentPath, segmentCount);
    segmentData->m_segmentFile = fopen(segmentPath.c_str(), "a");
    if (segmentData->m_segmentFile == nullptr) {
        int errCode = errno;
        ELOG_REPORT_SYS_ERROR(fopen, "Failed to open segment file %s: %d", segmentPath.c_str(),
                              errCode);
        delete segmentData;
        return false;
    }
    m_currentSegment.store(segmentData, std::memory_order_relaxed);
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
        // NOTE: if we failed to extract segment id from file - that is OK, since the directory
        // might contain log segmented with different name scheme
        if (getSegmentIndex(fileName, segmentIndex)) {
            if (maxSegmentIndex < segmentIndex) {
                maxSegmentIndex = segmentIndex;
                lastSegmentName = fileName;
            }
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
                ELOG_REPORT_WARN("Invalid segment file name: %s", fileName.c_str());
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

bool ELogSegmentedFileTarget::advanceSegment(uint32_t segmentId, const std::string& logMsg,
                                             uint64_t currentEpoch) {
    // quickly open a new segment, then switch segments
    // finally retire to GC previous segment
    // the segment data destructor is responsible for logging pending messages and closing segment
    // file in an orderly fashion
    // this method releases the current thread to do other stuff

    // TODO:
    // we might choose to have this configurable:
    // wait for epoch
    // OR
    // retire and go + GC recycle frequency
    // so if the recycle frequency is 0, it means wait for epoch
    // default value is zero, so we wait

    // open a new segment
    SegmentData* nextSegment = new (std::nothrow) SegmentData(segmentId);
    if (nextSegment == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate segment data for segment %u, out of memory",
                          segmentId);
        return false;
    }
    if (!nextSegment->m_pendingMsgs.initialize(ELOG_SEGMENT_PENDING_RING_SIZE)) {
        ELOG_REPORT_ERROR("Failed to initialize ring buffer of segment object");
        delete nextSegment;
        return false;
    }

    // create segment file
    std::string segmentPath;
    // TODO: when rotating segment id should be cycling
    formatSegmentPath(segmentPath, segmentId);
    // TODO: when rotating should truncate file rather than append
    nextSegment->m_segmentFile = fopen(segmentPath.c_str(), "a");
    if (nextSegment->m_segmentFile == nullptr) {
        ELOG_REPORT_SYS_ERROR(fopen, "Failed to open segment file %s", segmentPath.c_str());
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
    if (fputs(logMsg.c_str(), prevSegment->m_segmentFile) == EOF) {
        ELOG_REPORT_SYS_ERROR(fputs, "Failed to write to log file");
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
                const std::string& logMsg = prevSegment->m_pendingMsgs.back();
                logMsgs.push_front(logMsg);
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

    // log the last batch, there shouldn't be any more
    {
        // drain to a temp queue and write file outside lock scope to minimize lock waiting time
        // of other threads
        // std::unique_lock<std::mutex> lock(prevSegment->m_lock);
        ELOG_REPORT_TRACE("Logging %u final pending messages", prevSegment->m_pendingMsgs.size());
        // logMsgQueue(prevSegment->m_pendingMsgs, prevSegment->m_segmentFile);
        while (!prevSegment->m_pendingMsgs.empty()) {
            const std::string& logMsg = prevSegment->m_pendingMsgs.back();
            if (fputs(logMsg.c_str(), prevSegment->m_segmentFile) == EOF) {
                ELOG_REPORT_SYS_ERROR(fputs, "Failed to write to log file");
            }
            prevSegment->m_pendingMsgs.pop();
        }
    }

    // NOTE: only now we can close the segment (and this should also auto-flush)
    if (fclose(prevSegment->m_segmentFile) == -1) {
        ELOG_REPORT_SYS_ERROR(fclose, "Failed to close segment log file");
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
