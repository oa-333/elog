
#include "dbg_util.h"
#include "elog_api.h"
#include "life_sign_manager.h"

#ifdef ELOG_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#ifdef ELOG_WINDOWS
#include <tlhelp32.h>
#endif

#ifndef ELOG_MSVC
#include <readline/history.h>
#include <readline/readline.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#define ELOG_PM_VER_MAJOR 0
#define ELOG_PM_VER_MINOR 1

// command names
#define CMD_EXIT "exit"
#define CMD_HELP "help"
#define CMD_LS_SHM "ls-shm"
#define CMD_DUMP_SHM "dump-shm"
#define CMD_DEL_SHM "del-shm"
#define CMD_DEL_ALL_SHM "del-all-shm"

#ifndef ELOG_MSVC
static const char* sCommands[] = {CMD_EXIT,    CMD_HELP,        CMD_LS_SHM, CMD_DUMP_SHM,
                                  CMD_DEL_SHM, CMD_DEL_ALL_SHM, nullptr};
#endif

// error codes
#define ERR_INIT 1
#define ERR_LIST_SHM 2
#define ERR_OPEN_SHM 3
#define ERR_READ_SHM 4
#define ERR_CLOSE_SHM 5
#define ERR_DEL_SHM 6
#define ERR_SHM_NOT_FOUND 7
#define ERR_MISSING_ARG 8
#define ERR_INVALID_ARG 9

// CLI prompt
#define ELOG_PM_PROMPT "<elog-pm> $ "

#ifdef ELOG_WINDOWS
#define GUARDIAN_DEFAULT_SYNC_PERIOD_MILLIS 1000
#endif

#ifndef ELOG_MSVC
static char** elog_pm_complete_func(const char* text, int start, int end);
static char* elog_pm_cmd_generator_func(const char* text, int state);
static char* elog_pm_shm_generator_func(const char* text, int state);
#endif

// thread name map type
typedef std::unordered_map<uint64_t, std::string> ThreadNameMap;

// application data gathered from context records
struct AppData {
    std::string m_appName;
    ThreadNameMap m_threadNameMap;
};

static dbgutil::ShmSegmentList sSegmentList;

static elog::ELogLogger* sLogger = nullptr;

static bool sShouldTermDbgUtil = false;

static int initELog();
static void termELog();
static int initDbgUtil();
static void termDbgUtil();
static void runCliLoop();
static int listAllSegments(bool printList = true, const char* prefix = nullptr);
static int delAllSegments();
static int execDelShm(const std::string& shmName);
static int execDumpShm(const std::string& shmName);
static int displayShm(const char* shmName, uint32_t size);
static int processShm();
static void printLifeSignHeader(const dbgutil::LifeSignHeader* hdr, const AppData& appData);
static int getAppData(AppData& appData);
static int printThreadData(uint32_t threadSlotId, const AppData& appData, bool& entryUsed);
static int printLifeSignRecords(const dbgutil::LifeSignHeader* hdr, const AppData& appData);
static int printThreadLifeSignRecords(uint32_t threadSlotId, const dbgutil::LifeSignHeader* hdr,
                                      const AppData& appData);
static void printTime(const char* title, int64_t epochTimeMilliSeconds, uint32_t padding = 0);

// Windows Shared Memory Guardian Stuff
//
// the guardian process is required (on Windows only) for two purposes:
// (1) ensuring shared memory segments remain alive even if the process who creates them
// crashed (this is contrary to POSIX-compliant systems)
// (2) Continually synchronize shared memory segment contents to backing file, such that even
// after the guardian process terminates, the file contents can be used to read life-sign data.
// This also requires a change in elog_pm, such that if the segment cannot be opened (both the
// creating process and the guardian ended), the backing file can still be used.
//
// the design of the guardian is quite simple:
// every X seconds do the following:
// 1. list all segments
// 2. add new segments to managed segment list
// 3. remove segments in terminal state whose backing file has been deleted
// 4. try to open newly added segments (if failed then do analysis below and update file)
// 5. mark new segment state (alive/dead)
// 6. synchronize all live segments to disk
// 7. detect when segment is orphan
// 8. orphan segments that were fully synchronized to disk are in safe state, even if guardian
// terminates for some reason, so there should be some special marking for that

// TODO:
// if guardian fails to open shm then analysis should take place:
// - verify process is dead (what if not? just warn?)
// - check if fully synced flag is set, if so then we are done
// - check last sync time with last process time
//  - if greater then there is better chance of actually synced
//  - if less, then most probably we have partial info, can we make sure data is not corrupt? in
//    this case we actually would like to print raw data, so if we see life-sign header that is
//    more than the record count, we should print that record with hexa data or chars.
//
// currently the policy is just to open the backing file, create a mapping ojbect and read data from
// there (i.e. when dumping a shared memory segment).
//
// we should also distinguish between the cases of abrupt and orderly termination of the
// creating process - sync state, last sync time
//
// when a segment becomes orphan, it suffices to sync it to disk only once, and then the mapping
// can be closed. we should reserve some bit in the segment telling its state.
#ifdef ELOG_WINDOWS
static uint64_t sGuardianSyncPeriodMillis = GUARDIAN_DEFAULT_SYNC_PERIOD_MILLIS;
enum ShmSegmentState : uint32_t {
    SEG_INIT,     // just been discovered
    SEG_UNKNOWN,  // could not open shm segment (process probably dead, backing file state unknown)
    SEG_CORRUPT,  // could not infer pid from segment name
    SEG_ALIVE,    // process alive, segment opened successfully
    SEG_DEAD,     // process dead, segment opened successfully
    SEG_SYNCED,   // segment synced to disk, first time
    SEG_FULLY_SYNCED  // segment synced to disk, final time
};

struct ShmSegmentData {
    ShmSegmentState m_state;
    uint32_t m_size;
    uint64_t m_pid;
    int64_t m_lastProcessUpdateEpochMillis;
    int64_t m_lastSyncEpochMillis;
    uint32_t m_processState;  // 0 - unknown, 1 - alive, 2 - dead
    uint32_t m_fullySynced;
    dbgutil::OsShm* m_shm;
    dbgutil::LifeSignHeader* m_hdr;

    ShmSegmentData()
        : m_state(SEG_INIT),
          m_size(0),
          m_pid(0),
          m_lastProcessUpdateEpochMillis(0),
          m_lastSyncEpochMillis(0),
          m_processState(0),
          m_fullySynced(false),
          m_shm(nullptr),
          m_hdr(nullptr) {}
};

typedef std::unordered_map<std::string, ShmSegmentData> ShmSegmentMap;
static ShmSegmentMap sGuardedSegmentMap;
static const char* segStateToString(ShmSegmentState state);
static bool parseInt(const char* strValue, uint64_t& value);
static int runShmGuardian(int argc, char* argv[]);
static void guardShmSegments();
static void mergeGuardedSegments(const dbgutil::ShmSegmentList& segments,
                                 const std::unordered_set<DWORD>& pids, bool pidListFullyValid);
static void initSegmentData(const std::string& segName, ShmSegmentData& segData,
                            const std::unordered_set<DWORD>& pids, bool pidListFullyValid);
static void updateGuardedSegments(const std::unordered_set<DWORD>& pids, bool pidListFullyValid);
static void updateGuardedSegment(const std::string& segName, ShmSegmentData& segData,
                                 const std::unordered_set<DWORD>& pids, bool pidListFullyValid);
static uint64_t extractPid(const std::string& segName);
static bool getProcessList(std::unordered_set<DWORD>& pids);
static bool isProcessAlive(uint64_t pid, const std::unordered_set<DWORD>& pids);
static bool syncSegment(const std::string& segName, ShmSegmentData& segData);
#endif

inline void flushAllStream() {
    fflush(stdout);
    fflush(stderr);
}

static int execArgs(int argc, char* argv[]) {
// NOTE: under Windows a special mode is supported for keeping shared memory segments alive
// during application crash (otherwise the kernel object is destroyed)
#ifdef ELOG_WINDOWS
    if (argc == 2 && strcmp(argv[1], "--shm-guard") == 0) {
        return runShmGuardian(argc, argv);
    }
#endif

    // invalid usage
    if (argc == 1) {
        return ERR_INIT;
    }

    // parse user command
    if (strcmp(argv[1], CMD_LS_SHM) == 0) {
        return listAllSegments();
    }
    if (strcmp(argv[1], CMD_DEL_ALL_SHM) == 0) {
        return delAllSegments();
    }
    if (strcmp(argv[1], CMD_DUMP_SHM) == 0) {
        if (argc < 3) {
            ELOG_ERROR_EX(sLogger, "Missing argument for command " CMD_DUMP_SHM);
            return ERR_MISSING_ARG;
        }
        if (argc > 3) {
            ELOG_WARN_EX(sLogger, "Ignoring excess arguments passed to command " CMD_DUMP_SHM);
        }
        int res = listAllSegments(false);
        if (res != 0) {
            return ERR_LIST_SHM;
        }
        return execDumpShm(argv[2]);
    }
    if (strcmp(argv[1], CMD_DEL_SHM) == 0) {
        if (argc < 3) {
            ELOG_ERROR_EX(sLogger, "Missing argument for command " CMD_DEL_SHM);
            return ERR_MISSING_ARG;
        }
        if (argc > 3) {
            ELOG_WARN_EX(sLogger, "Ignoring excess arguments passed to command " CMD_DEL_SHM);
        }
        int res = listAllSegments(false);
        if (res != 0) {
            return ERR_LIST_SHM;
        }
        return execDelShm(argv[2]);
    }
    ELOG_ERROR_EX(sLogger, "Invalid command: %s", argv[2]);
    return ERR_INVALID_ARG;
}

int main(int argc, char* argv[]) {
    // initialize elog library
    int res = initELog();
    if (res != 0) {
        return res;
    }

    // connect to debug util library
    res = initDbgUtil();
    if (res != 0) {
        return res;
    }

    // run as utility or as interactive CLI
    if (argc >= 2) {
        res = execArgs(argc, argv);
    } else {
        runCliLoop();
    }

    termDbgUtil();
    termELog();
    return res;
}

static int initELog() {
    // NOTE: regardless of how ELog was built, we must disable life-sign reports (elog_pm does not
    // need them anyway), otherwise life-sign manager would complain that shm segment is already
    // created (when trying to open any segment)
    elog::ELogParams params;
    params.m_lifeSignParams.m_enableLifeSignReport = false;
    if (!elog::initialize(params)) {
        ELOG_ERROR_EX(sLogger, "Failed to initialize ELog library");
        return ERR_INIT;
    }

    // add stderr log target
    const char* cfg =
        "sys://stderr?name=elog_pm&"
        "enable_stats=no&"
        "log_format="
        "${time} "
        "${switch: ${level}:"
        "   ${case: ${const-level: NOTICE}: ${fmt:begin-fg-color=yellow}} :"
        "   ${case: ${const-level: WARN}: ${fmt:begin-fg-color=bright-yellow}} :"
        "   ${case: ${const-level: ERROR}: ${fmt:begin-fg-color=red}} :"
        "   ${case: ${const-level: FATAL}: ${fmt:begin-fg-color=bright-red}}"
        "}"
        "${level:6}${fmt:default} "
        "[${tid}] "
        "${src:font=underline} "
        "${msg}";
    elog::ELogTargetId logTargetId = elog::configureLogTarget(cfg);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        ELOG_ERROR_EX(sLogger, "Failed to configure stderr log target");
        elog::terminate();
        return ERR_INIT;
    }

    sLogger = elog::getSharedLogger("elog_pm", true, true);
    return 0;
}

static void termELog() {
    sLogger = nullptr;
    elog::terminate();
}

static int initDbgUtil() {
    if (!dbgutil::isDbgUtilInitialized()) {
        sShouldTermDbgUtil = true;
        dbgutil::DbgUtilErr rc = dbgutil::initDbgUtil(nullptr, DBGUTIL_DEFAULT_LOG_HANDLER,
                                                      dbgutil::LS_INFO, DBGUTIL_FLAGS_ALL);
        if (rc != DBGUTIL_ERR_OK) {
            ELOG_ERROR_EX(sLogger, "Failed to initialize dbgutil library: %s",
                          dbgutil::errorToString(rc));
            return ERR_INIT;
        }
    }
    return 0;
}

static void termDbgUtil() {
    if (sShouldTermDbgUtil) {
        dbgutil::termDbgUtil();
        sShouldTermDbgUtil = false;
    }
}

static void printLogo() {
    printf("ELog Post-mortem CLI, version %u.%u\n", ELOG_PM_VER_MAJOR, ELOG_PM_VER_MINOR);
}

static void printHelp() {
    printf("ELog Post-mortem CLI:\n");
    printf("q/quit/exit: exit from the cli\n");
    printf("ls-shm: list all shared memory segments\n");
    printf("del-all-shm: deletes all shared memory segments\n");
    printf("del-shm <name>: delete a shared memory segment\n");
    printf("dump-shm <name>: dumps the contents of a shared memory segment\n");
    printf("help: prints this help screen\n");
}

/** @brief Trims a string's prefix from the left side (in-place). */
inline void ltrim(std::string& s) { s.erase(0, s.find_first_not_of(" \n\r\t")); }

/** @brief Trims a string suffix from the right side (in-place). */
inline void rtrim(std::string& s) { s.erase(s.find_last_not_of(" \n\r\t") + 1); }

/** @brief Trims a string from both sides (in-place). */
inline std::string trim(const std::string& s) {
    std::string res = s;
    ltrim(res);
    rtrim(res);
    return res;
}

int execDelShm(const std::string& shmName) {
    dbgutil::DbgUtilErr rc =
        dbgutil::getLifeSignManager()->deleteLifeSignShmSegment(shmName.c_str());
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_ERROR_EX(sLogger, "Failed to delete shared memory segment %s: %s\n", shmName.c_str(),
                      dbgutil::errorToString(rc));
        return ERR_DEL_SHM;
    }
    return 0;
}

int execDumpShm(const std::string& shmName) {
    dbgutil::ShmSegmentList::const_iterator itr =
        std::find_if(sSegmentList.begin(), sSegmentList.end(),
                     [&shmName](const std::pair<std::string, uint32_t>& element) {
                         return element.first.compare(shmName) == 0;
                     });
    if (itr == sSegmentList.end()) {
        ELOG_ERROR_EX(sLogger, "Shared memory segment %s not found", shmName.c_str());
        return ERR_SHM_NOT_FOUND;
    }
    size_t size = itr->second;
    return displayShm(shmName.c_str(), size);
}

static bool execCommand(const std::string& cmd) {
    if (cmd.compare(CMD_EXIT) == 0 || cmd.compare("quit") == 0 || cmd.compare("q") == 0) {
        return false;
    }
    printf("\n");
    if (cmd.compare(CMD_HELP) == 0) {
        printHelp();
    } else if (cmd.compare(CMD_LS_SHM) == 0) {
        listAllSegments();
    } else if (cmd.compare(CMD_DEL_ALL_SHM) == 0) {
        delAllSegments();
    } else if (cmd.starts_with(CMD_DUMP_SHM)) {
        std::string shmName = trim(cmd.substr(strlen(CMD_DUMP_SHM)));
        execDumpShm(shmName);
    } else if (cmd.starts_with(CMD_DEL_SHM)) {
        std::string shmName = trim(cmd.substr(strlen(CMD_DEL_SHM)));
        execDelShm(shmName);
    } else {
        fprintf(stderr, "ERROR: Unrecognized command\n");
    }
    printf("\n");
    return true;
}

#ifndef ELOG_MSVC
void runCliLoop() {
    rl_attempted_completion_function = elog_pm_complete_func;
    rl_completion_entry_function = elog_pm_shm_generator_func;
    char* line = nullptr;
    printf("\n");
    while ((line = readline("<elog_pm> $ ")) != nullptr) {
        if (line && *line) {
            add_history(line);
        }
        bool shouldContinue = execCommand(trim(line));
        free(line);
        line = nullptr;
        if (!shouldContinue) {
            break;
        }
    }
}
#else
void runCliLoop() {
    char* input = nullptr;
    printLogo();
    printf("\n");
    while (true) {
        printf(ELOG_PM_PROMPT);
        std::string strCmd;
        while (strCmd.empty()) {
            strCmd = trim(strCmd);
            std::getline(std::cin, strCmd);
        }
        if (!execCommand(strCmd)) {
            break;
        }
    }
}
#endif

#ifndef ELOG_MSVC
char** elog_pm_complete_func(const char* text, int start, int end) {
    // attempt completion only at start of line
    // if we are at start of line, then we give back command names
    if (start == 0) {
        return rl_completion_matches(text, elog_pm_cmd_generator_func);
    }

    return nullptr;
}

char* elog_pm_cmd_generator_func(const char* text, int state) {
    static int listIndex = 0;
    static int len = 0;
    const char* name = nullptr;

    if (!state) {
        listIndex = 0;
        len = strlen(text);
    }

    while ((name = sCommands[listIndex++])) {
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    return nullptr;
}

char* elog_pm_shm_generator_func(const char* text, int state) {
    static int listIndex = 0;
    static int len = 0;

    if (!state) {
        listIndex = 0;
        len = strlen(text);
        if (listAllSegments(false) != 0) {
            return nullptr;
        }
    }

    while (listIndex < sSegmentList.size()) {
        const char* name = sSegmentList[listIndex++].first.c_str();
        if (len == 0 || strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    return nullptr;
}
#endif

int listAllSegments(bool printList /* = true */, const char* prefix /* = nullptr */) {
    ELOG_TRACE_EX(sLogger, "elog_pm searching for orphan life-sign shared memory segments...");

    sSegmentList.clear();
    dbgutil::DbgUtilErr rc = dbgutil::getLifeSignManager()->listLifeSignShmSegments(sSegmentList);
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_ERROR_EX(sLogger, "Failed to list shared memory segments: %s",
                      dbgutil::errorToString(rc));
        return ERR_LIST_SHM;
    }

    if (sSegmentList.empty()) {
        if (printList) {
            ELOG_INFO_EX(sLogger, "No shared memory segments found");
        }
        return 0;
    }

    // prune by prefix if needed
    if (prefix != nullptr) {
        std::erase_if(sSegmentList, [prefix](const std::pair<std::string, uint32_t>& element) {
            return element.first.starts_with(prefix);
        });
    }

    // sort in descending order by name, such that recent is first
    std::sort(sSegmentList.begin(), sSegmentList.end(),
              [](const std::pair<std::string, uint32_t>& lhs,
                 const std::pair<std::string, uint32_t>& rhs) {
                  return std::greater<std::string>()(lhs.first, rhs.first);
              });

    // display segments
    if (printList) {
        size_t maxNameSize = 0;
        for (const auto& entry : sSegmentList) {
            maxNameSize = std::max(maxNameSize, entry.first.length());
        }
        printf("Shared memory segment list:\n");
        // we subtract 2, because Name takes 4 characters, but there is also two spaces in between
        printf("Name%*sSize\n", (int)(maxNameSize - 2), "");
        for (const auto& entry : sSegmentList) {
            printf("%s  %u bytes\n", entry.first.c_str(), entry.second);
        }
        flushAllStream();
    }
    return 0;
}

int delAllSegments() {
    if (sSegmentList.empty()) {
        listAllSegments(false);
    }
    for (auto& shmSegment : sSegmentList) {
        ELOG_INFO_EX(sLogger, "Deleting shared memory segment %s", shmSegment.first.c_str());
        dbgutil::DbgUtilErr rc =
            dbgutil::getLifeSignManager()->deleteLifeSignShmSegment(shmSegment.first.c_str());
        if (rc != DBGUTIL_ERR_OK) {
            ELOG_ERROR_EX(sLogger, "Failed to delete shared memory segment %s: %s",
                          shmSegment.first.c_str(), dbgutil::errorToString(rc));
            return ERR_DEL_SHM;
        }
    }
    sSegmentList.clear();
    return 0;
}

int displayShm(const char* shmName, uint32_t size) {
    // open shared memory segment
    dbgutil::DbgUtilErr rc =
        dbgutil::getLifeSignManager()->openLifeSignShmSegment(shmName, size, false, true);
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_ERROR_EX(sLogger, "Failed to open shared memory segment %s with %u bytes: %s", shmName,
                      size, dbgutil::errorToString(rc));
        return ERR_OPEN_SHM;
    }

    // now process context records and extract process name and thread names
    int res = processShm();

    // close and return
    rc = dbgutil::getLifeSignManager()->closeLifeSignShmSegment();
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_ERROR_EX(sLogger, "Failed to close shared memory segment %s with %u bytes: %s",
                      shmName, size, dbgutil::errorToString(rc));
        return ERR_CLOSE_SHM;
    }
    return res;
}

int processShm() {
    // process context records and extract process name and thread names
    AppData appData;
    int res = getAppData(appData);
    if (res != 0) {
        return res;
    }

    // now we can print header
    dbgutil::LifeSignHeader* hdr = nullptr;
    dbgutil::DbgUtilErr rc = dbgutil::getLifeSignManager()->readLifeSignHeader(hdr);
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_ERROR_EX(sLogger, "Failed to read life-sign header: %s", dbgutil::errorToString(rc));
        return ERR_READ_SHM;
    }
    printLifeSignHeader(hdr, appData);

    // print life sign records of all threads
    res = printLifeSignRecords(hdr, appData);
    if (res != 0) {
        return res;
    }

    return 0;
}

void printLifeSignHeader(const dbgutil::LifeSignHeader* hdr, const AppData& appData) {
    const char* headers[] = {"Image path",
                             "Application name",
                             "Start of run",
                             "Process id",
                             "Context area size",
                             "Life-sign area size"
#ifdef ELOG_WINDOWS
                             ,
                             "Last process seen time",
                             "Last segment sync time",
                             "Is fully synced"
#endif
    };
    const size_t headerCount = sizeof(headers) / sizeof(headers[0]);
    size_t maxHeaderLength = 0;
    for (uint32_t i = 0; i < headerCount; ++i) {
        maxHeaderLength = std::max(maxHeaderLength, strlen(headers[i]));
    }
    int headerPad[headerCount] = {};
    for (uint32_t i = 0; i < headerCount; ++i) {
        headerPad[i] = (int)(maxHeaderLength - strlen(headers[i]));
    }

    printf("Shared memory segment details:\n");
    printf("--------------------------------------------\n");
    printf("Image path: %*s%s\n", headerPad[0], "", hdr->m_imagePath);
    printf("Application name: %*s%s\n", headerPad[1], "", appData.m_appName.c_str());
    printTime("Start of run", hdr->m_startTimeEpochMilliSeconds, headerPad[2]);
    printf("Process id: %*s%u\n", headerPad[3], "", hdr->m_pid);
    printf("Context area size: %*s%u bytes\n", headerPad[4], "", hdr->m_contextAreaSize);
    printf("Life-sign area size: %*s%u bytes\n", headerPad[5], "", hdr->m_contextAreaSize);
#ifdef ELOG_WINDOWS
    printTime("Last process seen time", hdr->m_lastProcessTimeEpochMillis, headerPad[6]);
    printTime("Last segment sync time", hdr->m_lastSyncTimeEpochMillis, headerPad[7]);
    printf("Is fully synced: %*s%s\n", headerPad[8], "", hdr->m_isFullySynced ? "yes" : "no");
#endif
    printf("--------------------------------------------\n");
    flushAllStream();
}

int getAppData(AppData& appData) {
    // read context records
    uint32_t offset = 0;
    char* recPtr = nullptr;
    uint32_t recLen = 0;
    bool done = false;
    while (!done) {
        dbgutil::DbgUtilErr rc =
            dbgutil::getLifeSignManager()->readContextRecord(offset, recPtr, recLen);
        if (rc == DBGUTIL_ERR_END_OF_STREAM) {
            break;
        }
        if (rc != DBGUTIL_ERR_OK) {
            ELOG_ERROR_EX(sLogger, "Failed to read context record at offset %u: %s", offset,
                          dbgutil::errorToString(rc));
            return ERR_READ_SHM;
        }
        uint32_t recordType = *(uint32_t*)recPtr;
        uint32_t localOffset = sizeof(uint32_t);
        if (recordType == 0) {
            appData.m_appName = (recPtr + localOffset);
        } else if (recordType == 1) {
            uint64_t threadId = *(uint64_t*)(recPtr + localOffset);
            localOffset += sizeof(uint64_t);
            std::pair<ThreadNameMap::iterator, bool> resPair = appData.m_threadNameMap.insert(
                ThreadNameMap::value_type(threadId, recPtr + localOffset));
            if (!resPair.second) {
                ELOG_ERROR_EX(sLogger,
                              "WARNING: duplicate thread id %" PRIu64
                              ", replacing name %s with name %s",
                              threadId, resPair.first->second.c_str(), recPtr + localOffset);
                resPair.first->second = recPtr + localOffset;
            }
        }
    }

    return 0;
}

int printThreadData(uint32_t threadSlotId, const AppData& appData, bool& entryUsed) {
    uint64_t threadId = 0;
    int64_t startEpoch = 0;
    int64_t endEpoch = 0;
    bool isRunning = false;
    uint32_t useCount = 0;
    dbgutil::DbgUtilErr rc = dbgutil::getLifeSignManager()->readThreadLifeSignDetails(
        threadSlotId, threadId, startEpoch, endEpoch, isRunning, useCount);
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_ERROR_EX(sLogger, "Failed to read life-sign details of thread at slot %u: %s",
                      threadSlotId, dbgutil::errorToString(rc));
        return ERR_READ_SHM;
    }

    if (useCount == 0) {
        // never been used
        entryUsed = false;
        return 0;
    }

    const char* headers[] = {"Thread id", "Thread name", "Thread state", "Thread start time",
                             "Thread end time"};
    const size_t headerCount = sizeof(headers) / sizeof(headers[0]);
    size_t maxHeaderLength = 0;
    for (uint32_t i = 0; i < headerCount; ++i) {
        maxHeaderLength = std::max(maxHeaderLength, strlen(headers[i]));
    }
    int headerPad[headerCount] = {};
    for (uint32_t i = 0; i < headerCount; ++i) {
        headerPad[i] = (int)(maxHeaderLength - strlen(headers[i]));
    }

    printf("Thread id: %*s%" PRIu64 "\n", headerPad[0], "", threadId);
    ThreadNameMap::const_iterator itr = appData.m_threadNameMap.find(threadId);
    if (itr == appData.m_threadNameMap.end()) {
        printf("Thread name: %*sN/A\n", headerPad[1], "");
    } else {
        printf("Thread name: %*s%s\n", headerPad[1], "", itr->second.c_str());
    }
    printf("Thread state: %*s%s\n", headerPad[2], "", isRunning ? "running" : "terminated");
    printTime("Thread start time", startEpoch, headerPad[3]);
    if (!isRunning) {
        printTime("Thread end time", endEpoch, headerPad[4]);
    }
    flushAllStream();
    entryUsed = true;
    return 0;
}

int printLifeSignRecords(const dbgutil::LifeSignHeader* hdr, const AppData& appData) {
    for (uint32_t i = 0; i < hdr->m_maxThreads; ++i) {
        bool entryUsed = false;
        int res = printThreadData(i, appData, entryUsed);
        if (res != 0) {
            return res;
        }

        if (!entryUsed) {
            // never been used
            continue;
        }

        res = printThreadLifeSignRecords(i, hdr, appData);
        if (res != 0) {
            return res;
        }
    }

    return 0;
}

void printTime(const char* title, int64_t epochTimeMilliSeconds, uint32_t padding /* = 0 */) {
#if ELOG_CPP_VER >= 202002L
    std::chrono::sys_time<std::chrono::milliseconds> tp{
        std::chrono::milliseconds(epochTimeMilliSeconds)};
    std::chrono::zoned_time<std::chrono::milliseconds> zt(std::chrono::current_zone(), tp);
    std::string timeStr = std::format("{:%Y-%m-%d %H:%M:%S}", zt.get_local_time());
    printf("%s: %*s%s\n", title, padding, "", timeStr.c_str());
#else
    time_t rawTime = epochTimeMilliSeconds / 1000;
    struct tm* timeInfo = localtime(&rawTime);
    char timeStr[64] = {};
    size_t offset = strftime(timeStr, 64, "%Y-%m-%d %H:%M:%S %Z", timeInfo);
    printf("%s: %*s%s\n", title, padding, "", timeStr);
#endif
}

int printThreadLifeSignRecords(uint32_t threadSlotId, const dbgutil::LifeSignHeader* hdr,
                               const AppData& appData) {
    // read context records
    uint32_t offset = 0;
    char* recPtr = nullptr;
    uint32_t recLen = 0;
    bool done = false;
    bool callerShouldRelease = false;
    printf("Thread life-sign records:\n");
    uint32_t i = 1;
    while (!done) {
        dbgutil::DbgUtilErr rc = dbgutil::getLifeSignManager()->readLifeSignRecord(
            threadSlotId, offset, recPtr, recLen, callerShouldRelease);
        if (rc == DBGUTIL_ERR_END_OF_STREAM) {
            break;
        }
        if (rc != DBGUTIL_ERR_OK) {
            ELOG_ERROR_EX(sLogger, "Failed to read context record at offset %u: %s", offset,
                          dbgutil::errorToString(rc));
            return ERR_READ_SHM;
        }
        printf("%u. %.*s\n", i, recLen, recPtr);
        ++i;
        flushAllStream();
        if (callerShouldRelease) {
            dbgutil::getLifeSignManager()->releaseLifeSignRecord(recPtr);
        }
    }
    printf("--------------------------------------------\n");
    flushAllStream();

    return 0;
}

#ifdef ELOG_WINDOWS
static const char* segStateToString(ShmSegmentState state) {
    switch (state) {
        case SEG_INIT:
            return "INIT";
        case SEG_UNKNOWN:
            return "UNKNOWN";
        case SEG_CORRUPT:
            return "CORRUPT";
        case SEG_ALIVE:
            return "ALIVE";
        case SEG_DEAD:
            return "DEAD";
        case SEG_SYNCED:
            return "SYNCED";
        case SEG_FULLY_SYNCED:
            return "FULLY-SYNCED";
        default:
            return "N/A";
    }
}

bool parseInt(const char* strValue, uint64_t& value) {
    std::size_t pos = 0;
    try {
        value = std::stoull(strValue, &pos);
    } catch (std::exception& e) {
        ELOG_ERROR_EX(sLogger, "Invalid integer value '%s': %s", strValue, e.what());
        return false;
    }
    if (pos != strlen(strValue)) {
        ELOG_ERROR_EX(sLogger, "Excess characters at integer value: %s", strValue);
        return false;
    }
    return true;
}

int runShmGuardian(int argc, char* argv[]) {
    if (argc >= 3 && strcmp(argv[2], "--shm-sync-period") == 0) {
        if (argc < 4) {
            ELOG_ERROR_EX(sLogger, "Missing argument after --shm-sync-period");
            return ERR_INIT;
        }
        if (!parseInt(argv[3], sGuardianSyncPeriodMillis)) {
            ELOG_ERROR_EX(
                sLogger,
                "Invalid argument for --shm-sync-period, expecting positive integer value");
            return ERR_INIT;
        }
    }

    // create named mutex so that we can have only one such process
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "elog_windows_shm_guardian");
    if (hMutex == NULL) {
        ELOG_WIN32_ERROR_EX(sLogger, CreateMutexA,
                            "Failed to create ELog Life-Sign Guardian shared mutex");
        return ERR_INIT;
    }
    DWORD res = GetLastError();
    if (res == ERROR_ALREADY_EXISTS) {
        ELOG_ERROR_EX(
            sLogger,
            "Cannot run ELog Life-Sign Guardian, there is already another instance running");
        CloseHandle(hMutex);
        return ERR_INIT;
    }

    bool done = false;
    while (!done) {
        guardShmSegments();
        std::this_thread::sleep_for(std::chrono::milliseconds(sGuardianSyncPeriodMillis));
        // TODO: determine exit conditions for guardian, or does it continue forever?
    }

    CloseHandle(hMutex);
    return 0;
}

void guardShmSegments() {
    // list all segments
    dbgutil::ShmSegmentList segments;
    dbgutil::DbgUtilErr rc = dbgutil::getLifeSignManager()->listLifeSignShmSegments(segments);
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_ERROR_EX(sLogger, "Failed to list shared memory segments: %s",
                      dbgutil::errorToString(rc));
        return;
    }

    std::unordered_set<DWORD> pids;
    bool pidListFullyValid = getProcessList(pids);

    // add new segments, infer state
    mergeGuardedSegments(segments, pids, pidListFullyValid);

    // update all segments (check process state, sync, update shm state)
    updateGuardedSegments(pids, pidListFullyValid);
}

void mergeGuardedSegments(const dbgutil::ShmSegmentList& segments,
                          const std::unordered_set<DWORD>& pids, bool pidListFullyValid) {
    // insert all new segments
    for (const auto& seg : segments) {
        std::pair<ShmSegmentMap::iterator, bool> resPair =
            sGuardedSegmentMap.insert(ShmSegmentMap::value_type(seg.first, ShmSegmentData()));
        if (resPair.second) {
            resPair.first->second.m_size = seg.second;
            ELOG_INFO_EX(sLogger, "Found new shared memory segment: %s", seg.first.c_str());
            initSegmentData(seg.first, resPair.first->second, pids, pidListFullyValid);
        }
    }

    // remove segments that were deleted, but only for dead processes
    ShmSegmentMap::iterator itr = sGuardedSegmentMap.begin();
    while (itr != sGuardedSegmentMap.end()) {
        // process next monitored segment
        const std::string& segName = itr->first;

        // search for segment in recent segment list
        dbgutil::ShmSegmentList::const_iterator itr2 =
            std::find_if(segments.begin(), segments.end(),
                         [&segName](const std::pair<std::string, uint32_t>& element) {
                             return element.first.compare(segName) == 0;
                         });

        // handle segment not found
        if (itr2 == segments.end()) {
            // if segment is final state then it can be removed
            ShmSegmentData& segData = itr->second;
            if (segData.m_state == SEG_FULLY_SYNCED || segData.m_state == SEG_CORRUPT ||
                segData.m_state == SEG_UNKNOWN) {
                ELOG_NOTICE_EX(
                    sLogger,
                    "Removing segment %s from monitored segments, segment is in terminal "
                    "state %s and backing file was deleted",
                    segName.c_str(), segStateToString(segData.m_state));
                itr = sGuardedSegmentMap.erase(itr);
                continue;
            }
        }

        // check next segment
        ++itr;
    }
}

void initSegmentData(const std::string& segName, ShmSegmentData& segData,
                     const std::unordered_set<DWORD>& pids, bool pidListFullyValid) {
    // the most urgent task is to open a file mapping to the shared memory segment before the
    // creating process might crash, and the segment is lost forever
    bool backingFileMapped = false;
    if (segData.m_shm == nullptr) {
        segData.m_shm = dbgutil::createOsShm();
    }
    if (segData.m_shm == nullptr) {
        ELOG_ERROR_EX(sLogger, "Failed to create shared memory object, out of memory");
        // remain at init state
    } else {
        dbgutil::DbgUtilErr rc =
            segData.m_shm->openShm(segName.c_str(), segData.m_size, true, true, &backingFileMapped);
        if (rc != DBGUTIL_ERR_OK) {
            ELOG_ERROR_EX(sLogger,
                          "Failed to open shared memory segment by name %s, with total size %u: %s",
                          segName.c_str(), segData.m_size, dbgutil::errorToString(rc));
            delete segData.m_shm;
            segData.m_shm = nullptr;
            segData.m_state = SEG_UNKNOWN;
        } else {
            ELOG_INFO_EX(sLogger, "Shared memory segment %s opened for reading by guardian",
                         segName.c_str());
            segData.m_hdr = (dbgutil::LifeSignHeader*)segData.m_shm->getShmPtr();
        }
    }

    // infer from segment name - pid
    if (segData.m_state != SEG_UNKNOWN) {
        segData.m_pid = extractPid(segName);
        if (segData.m_pid == 0) {
            segData.m_state = SEG_CORRUPT;
        } else {
            ELOG_INFO_EX(sLogger, "Extracted process id %" PRIu64 " for shared memory segment %s",
                         segData.m_pid, segName.c_str());
            if (isProcessAlive(segData.m_pid, pids)) {
                segData.m_state = SEG_ALIVE;
                ELOG_INFO_EX(sLogger, "Owning process of shared memory segment %s is still alive",
                             segName.c_str());
            } else {
                if (pidListFullyValid) {
                    if (segData.m_hdr->m_isFullySynced) {
                        segData.m_state = SEG_FULLY_SYNCED;
                        ELOG_INFO_EX(sLogger,
                                     "Owning process of shared memory segment %s is already dead, "
                                     "and the segment was fully synchronized",
                                     segName.c_str());
                        delete segData.m_shm;
                        segData.m_shm = nullptr;
                    } else {
                        if (backingFileMapped) {
                            segData.m_state = SEG_UNKNOWN;
                            ELOG_INFO_EX(
                                sLogger,
                                "Owning process of shared memory segment %s is already dead, "
                                "segment state is unknown",
                                segName.c_str());
                            delete segData.m_shm;
                            segData.m_shm = nullptr;
                        } else {
                            // we were able to open the shm just before the process died
                            segData.m_state = SEG_DEAD;
                            ELOG_INFO_EX(
                                sLogger,
                                "Owning process of shared memory segment %s is already dead",
                                segName.c_str());
                        }
                    }
                } else {
                    // otherwise remain at init
                    ELOG_WARN_EX(
                        sLogger,
                        "Failed to determine state of shred memory segment %s owning process",
                        segName.c_str());
                }
            }
        }
    }

    // rest of the members will be updated later
}

void updateGuardedSegments(const std::unordered_set<DWORD>& pids, bool pidListFullyValid) {
    for (auto& entry : sGuardedSegmentMap) {
        updateGuardedSegment(entry.first, entry.second, pids, pidListFullyValid);
    }
}

void updateGuardedSegment(const std::string& segName, ShmSegmentData& segData,
                          const std::unordered_set<DWORD>& pids, bool pidListFullyValid) {
    // check init state
    if (segData.m_state == SEG_INIT) {
        initSegmentData(segName, segData, pids, pidListFullyValid);
    }

    // check final state
    if (segData.m_state == SEG_FULLY_SYNCED || segData.m_state == SEG_CORRUPT ||
        segData.m_state == SEG_UNKNOWN) {
        ELOG_TRACE_EX(sLogger, "Skipping segment %s in terminal state %u", segName.c_str(),
                      (unsigned)segData.m_state);
        return;
    }

    // update process live state
    if (segData.m_state == SEG_ALIVE) {
        if (!isProcessAlive(segData.m_pid, pids)) {
            if (pidListFullyValid) {
                segData.m_state = SEG_DEAD;
                segData.m_hdr->m_isProcessAlive = 0;
                ELOG_INFO_EX(sLogger, "Owning process of shared memory segment %s died",
                             segName.c_str());
            } else {
                ELOG_WARN_EX(sLogger,
                             "Failed to determine state of shred memory segment %s owning process",
                             segName.c_str());
            }
        } else {
            segData.m_hdr->m_isProcessAlive = 1;
            segData.m_hdr->m_lastProcessTimeEpochMillis = elog::getCurrentTimeMillis();
            ELOG_TRACE_EX(sLogger, "Owning process of shared memory segment %s is still alive",
                          segName.c_str());
        }
    }

    // sync and advance state
    if (syncSegment(segName, segData)) {
        segData.m_hdr->m_lastSyncTimeEpochMillis = elog::getCurrentTimeMillis();
        if (segData.m_state == SEG_DEAD) {
            segData.m_state = SEG_SYNCED;
            segData.m_hdr->m_isFullySynced = 1;
            ELOG_INFO_EX(sLogger, "Shared memory segment %s synchronized to disk", segName.c_str());
        } else if (segData.m_state == SEG_SYNCED) {
            segData.m_state = SEG_FULLY_SYNCED;
            ELOG_INFO_EX(sLogger, "Shared memory segment %s fully synchronized to disk",
                         segName.c_str());
            if (segData.m_shm != nullptr) {
                segData.m_shm->closeShm();
                delete segData.m_shm;
                segData.m_shm = nullptr;
            }
        }
    }
}

uint64_t extractPid(const std::string& segName) {
    // segment name is composed as follows:
    // dbgutil.<process-name>.<tstamp>.<pid>.shm
    // so we need to find two last dots in the name and convert to int what is in between
    // the process name might contain dots so we don't know how many dots there are
    std::string::size_type prevDotPos = std::string::npos;
    std::string::size_type dotPos = std::string::npos;
    std::string::size_type nextDotPos = segName.find('.');
    do {
        prevDotPos = dotPos;
        dotPos = nextDotPos;
        if (nextDotPos != std::string::npos) {
            nextDotPos = segName.find('.', nextDotPos + 1);
        }
    } while (nextDotPos != std::string::npos);

    if (prevDotPos == std::string::npos || dotPos == std::string::npos) {
        ELOG_ERROR_EX(sLogger, "Invalid shared memory segment name, could not extract pid: %s",
                      segName.c_str());
        return 0;
    }
    std::string pidStr = segName.substr(prevDotPos + 1, dotPos - prevDotPos - 1);

    uint64_t pid = 0;
    if (!parseInt(pidStr.c_str(), pid)) {
        return 0;
    }
    return pid;
}

bool getProcessList(std::unordered_set<DWORD>& pids) {
    HANDLE hProcess;
    DWORD dwPriorityClass;

    // Take a snapshot of all processes in the system
    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE) {
        ELOG_WIN32_ERROR_EX(sLogger, CreateToolhelp32Snapshot,
                            "Failed to get process list snapshot");
        return false;
    }

    // Set the size of the structure before using it
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    // Retrieve information about the first process,
    // and exit if unsuccessful
    if (!Process32First(hProcessSnap, &pe32)) {
        ELOG_WIN32_ERROR_EX(sLogger, Process32First,
                            "Failed to start iterating process list snapshot");
        CloseHandle(hProcessSnap);
        return false;
    }

    // Now walk the snapshot of processes, and
    // display information about each process in turn
    do {
        pids.insert(pe32.th32ProcessID);
    } while (Process32Next(hProcessSnap, &pe32));
    DWORD err = GetLastError();

    CloseHandle(hProcessSnap);
    return err == ERROR_NO_MORE_FILES;
}

bool isProcessAlive(uint64_t pid, const std::unordered_set<DWORD>& pids) {
    return pids.find(pid) != pids.end();
}

bool syncSegment(const std::string& segName, ShmSegmentData& segData) {
    dbgutil::DbgUtilErr rc = segData.m_shm->syncShm();
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_ERROR_EX(sLogger, "Failed to synchronize shared memory segment %s to disk: %s",
                      segName.c_str(), dbgutil::errorToString(rc));
        return false;
    }
    return true;
}

#endif