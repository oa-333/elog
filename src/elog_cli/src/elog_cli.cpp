
#include "elog_api.h"

#ifdef ELOG_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#ifndef ELOG_MSVC
#include <readline/history.h>
#include <readline/readline.h>
#endif

#include <string>
#include <vector>

#include "elog_config_service_client.h"

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS
#include "elog_config_service_redis_reader.h"
#endif

#define ELOG_CLI_VER_MAJOR 0
#define ELOG_CLI_VER_MINOR 1

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS
#define ELOG_CLI_HAS_SERVICE_DISCOVERY
#define ELOG_CLI_SERVICE_DISCOVERY_NAME "redis"
#endif

// command names
#define CMD_EXIT "exit"
#define CMD_HELP "help"
#ifdef ELOG_CLI_HAS_SERVICE_DISCOVERY
#define CMD_LIST "list"
#endif
#define CMD_CONNECT "connect"
#define CMD_DISCONNECT "disconnect"
#define CMD_QUERY_LOG_LEVEL "query-log-level"
#define CMD_UPDATE_LOG_LEVEL "update-log-level"

#ifndef ELOG_MSVC
static const char* sCommands[] = {
    CMD_EXIT, CMD_HELP, CMD_CONNECT, CMD_DISCONNECT, CMD_QUERY_LOG_LEVEL, CMD_UPDATE_LOG_LEVEL,
#ifdef ELOG_CLI_HAS_SERVICE_DISCOVERY
    CMD_LIST,
#endif
    nullptr};
#endif

// error codes
#define ERR_INIT 1
#define ERR_START 2
#define ERR_CONNECT 3
#define ERR_STOP 4
#define ERR_TERM 5
#define ERR_QUERY 6
#define ERR_NOT_READY 7
#define ERR_MISSING_ARG 8
#define ERR_INVALID_ARG 9
#define ERR_EXEC 10

// CLI prompt
#define ELOG_CLI_PROMPT "<elog-cli> $ "

#ifndef ELOG_MSVC
static char** elog_cli_complete_func(const char* text, int start, int end);
static char* elog_cli_cmd_generator_func(const char* text, int state);
static char* elog_cli_completion_entry_func(const char* text, int state);
static char* elog_cli_log_source_generator_func(const char* text, int state);
#ifdef ELOG_CLI_HAS_SERVICE_DISCOVERY
static char* elog_cli_config_service_generator_func(const char* text, int state);
#endif
#endif

static elog::ELogLogger* sLogger = nullptr;
static elog::ELogConfigServiceClient sConfigServiceClient;
static bool sConnected = false;
static std::vector<std::string> sLogSources;

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS
static elog::ELogConfigServiceRedisReader sConfigServiceReader;
#endif

#ifdef ELOG_CLI_HAS_SERVICE_DISCOVERY
static std::vector<std::string> sServiceList;
static elog::ELogConfigServiceMap sServiceMap;
#endif

// cli skeleton
static int initELog();
static void termELog();
static void runCliLoop();

// commands
#ifdef ELOG_CLI_HAS_SERVICE_DISCOVERY
static int listServices();
static int printServices();
#endif
static int connectToELogProcess(const char* host, int port);
static int disconnectFromELogProcess();
static int queryLogLevel(const char* includeRegEx = ".*", const char* excludeRegEx = "");
static int updateLogLevels(const char* logLevelCfg);
static bool parseLogLevel(const char* logLevelStr, elog::ELogLevel& logLevel,
                          elog::ELogPropagateMode& propagateMode);

// helpers
static bool parseHostPort(const std::string& addr, std::string& host, int& port);
static void tokenize(const char* str, std::vector<std::string>& tokens,
                     const char* delims = " \t\r\n");
static bool listAllLogSources();

static void getEnvVar(const char* name, std::string& value) {
    char* envVarValueLocal = getenv(name);
    if (envVarValueLocal != nullptr) {
        value = envVarValueLocal;
    }
}

static int execArgs(int argc, char* argv[]) {
    // invalid usage
    if (argc == 1) {
        return ERR_INIT;
    }

    std::string host;
    int port = 0;
    bool query = false;
    std::string includeRegEx;
    std::string excludeRegEx;
    std::string updateCmd;

    for (int i = 1; i < argc; ++i) {
#ifdef ELOG_CLI_HAS_SERVICE_DISCOVERY
        if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            return printServices();
        }
#endif
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--host") == 0) {
            if (++i >= argc) {
                ELOG_ERROR_EX(sLogger, "Missing host parameter\n");
                return ERR_MISSING_ARG;
            }
            host = argv[i];
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (++i >= argc) {
                ELOG_ERROR_EX(sLogger, "Missing host parameter");
                return ERR_MISSING_ARG;
            }
            port = std::strtol(argv[i], nullptr, 10);
            if (port == 0) {
                ELOG_ERROR_EX(sLogger, "Invalid port parameter: %s", argv[i]);
                return ERR_INVALID_ARG;
            }
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--query") == 0) {
            query = true;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--include") == 0) {
            if (++i >= argc) {
                ELOG_ERROR_EX(sLogger, "Missing include filter parameter\n");
                return ERR_MISSING_ARG;
            }
            includeRegEx = argv[i];
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--exclude") == 0) {
            if (++i >= argc) {
                ELOG_ERROR_EX(sLogger, "Missing exclude filter parameter\n");
                return ERR_MISSING_ARG;
            }
            excludeRegEx = argv[i];
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--update") == 0) {
            if (++i >= argc) {
                ELOG_ERROR_EX(sLogger, "Missing update command parameter");
                return ERR_MISSING_ARG;
            }
            updateCmd = argv[i];
        } else {
            ELOG_ERROR_EX(sLogger, "Invalid argument: %s", argv[i]);
            return ERR_INVALID_ARG;
        }
    }

    if (host.empty() || port == 0) {
        ELOG_ERROR_EX(sLogger, "Missing host or port");
        return ERR_MISSING_ARG;
    }
    if (query && !updateCmd.empty()) {
        ELOG_ERROR_EX(sLogger, "Cannot specify query and update command together");
        return ERR_INVALID_ARG;
    }

    int res = connectToELogProcess(host.c_str(), port);
    if (res != 0) {
        return res;
    }

    if (query) {
        // for now only inclusion
        res = queryLogLevel(includeRegEx.c_str(), excludeRegEx.c_str());
    } else {
        res = updateLogLevels(updateCmd.c_str());
    }

    disconnectFromELogProcess();
    return res;
}

int main(int argc, char* argv[]) {
    // initialize elog library
    int res = initELog();
    if (res != 0) {
        return res;
    }

    // run as utility or as interactive CLI
    if (argc >= 2) {
        res = execArgs(argc, argv);
    } else {
        runCliLoop();
    }

    termELog();
    return res;
}

static int initELog() {
    // NOTE: regardless of how ELog was built, we must disable life-sign reports (elog_cli does not
    // need them anyway), otherwise life-sign manager would complain that shm segment is already
    // created (when trying to open any segment)
    elog::ELogParams params;
    params.m_configServiceParams.m_enableConfigService = false;
    if (!elog::initialize(params)) {
        ELOG_ERROR_EX(sLogger, "Failed to initialize ELog library");
        return ERR_INIT;
    }

    // add stderr log target
    const char* cfg =
        "sys://stderr?name=elog_cli&"
        "enable_stats=no&"
        "log_format="
        //"${time} "
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

    sLogger = elog::getSharedLogger("elog_cli", true, true);
    return 0;
}

static void termELog() {
    sLogger = nullptr;
    elog::terminate();
}

static void printLogo() {
    printf("ELog Configuration CLI, version %u.%u\n", ELOG_CLI_VER_MAJOR, ELOG_CLI_VER_MINOR);
}

static void printHelp() {
    printf("ELog Configuration CLI:\n\n");
    printf("q/quit/exit:     exit from the cli\n");
#ifdef ELOG_CLI_HAS_SERVICE_DISCOVERY
    printf("list:            lists all ELog services registered at %s cluster\n",
           ELOG_CLI_SERVICE_DISCOVERY_NAME);
#endif
    printf("connect:         connect to an ELog configuration service\n");
    printf("disconnect:      disconnect from an ELog configuration service\n");
    printf("query-log-level: queries for the log levels in the connected target process\n");
    printf("set-log-level:   configures the log levels for the connected target process\n");
    printf("help:            prints this help screen\n\n");
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

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS
static int listServices() {
    std::string redisServerList;
    std::string redisKey;
    std::string redisPassword;

    // first check in env
    getEnvVar("ELOG_REDIS_SERVERS", redisServerList);
    getEnvVar("ELOG_REDIS_KEY", redisKey);
    getEnvVar("ELOG_REDIS_PASSWORD", redisPassword);

    if (!sConfigServiceReader.setServerList(redisServerList.c_str())) {
        return 1;
    }
    if (!sConfigServiceReader.initialize()) {
        return 1;
    }

    // get raw service map
    elog::ELogConfigServiceMap rawServiceMap;
    if (!sConfigServiceReader.listServices(rawServiceMap)) {
        return 2;
    }

    sServiceMap.clear();
    sServiceList.clear();
    for (const auto& pair : rawServiceMap) {
        const std::string& service = pair.first;
        const std::string& appName = pair.second;
        std::vector<std::string> tokens;
        tokenize(service.c_str(), tokens, ":");
        if (tokens.size() != 3) {
            ELOG_WARN_EX(sLogger,
                         "Unexpected service name, expecting 3 tokens separated by colon: %s",
                         service.c_str());
            continue;
        }
        if (tokens[0].compare("elog_config_service") != 0) {
            ELOG_WARN_EX(
                sLogger,
                "Invalid service name, first token expected to be 'elog_config_service': %s",
                service.c_str());
            continue;
        }
        char* endPtr = nullptr;
        int port = std::strtol(tokens[2].c_str(), &endPtr, 10);
        if (endPtr == tokens[2].c_str() || errno == ERANGE) {
            ELOG_WARN_EX(sLogger, "Invalid port specification '%s' in service: %s",
                         tokens[2].c_str(), service.c_str());
            continue;
        }
        std::string serviceDetails = tokens[1] + ":" + tokens[2];
        sServiceList.push_back(serviceDetails);
        sServiceMap.insert(elog::ELogConfigServiceMap::value_type(serviceDetails, appName));
    }
    return 0;
}

static int printServices() {
    int res = listServices();
    if (res != 0) {
        return res;
    }
    for (const auto& entry : sServiceMap) {
        fprintf(stderr, "%s %s\n", entry.first.c_str(), entry.second.c_str());
    }
    return 0;
}
#endif

int connectToELogProcess(const char* host, int port) {
    if (sConnected) {
        ELOG_ERROR("Cannot connect, already connected to ELog process");
        return 1;
    }
    if (!sConfigServiceClient.initialize(host, port)) {
        ELOG_ERROR_EX(sLogger, "Failed to initialize configuration service client");
        return ERR_INIT;
    }
    if (!sConfigServiceClient.start()) {
        ELOG_ERROR_EX(sLogger, "Failed to start configuration service client");
        sConfigServiceClient.terminate();
        return ERR_START;
    }
    if (!sConfigServiceClient.waitReady()) {
        ELOG_ERROR_EX(sLogger, "Failed waiting for configuration service client to be ready");
        sConfigServiceClient.stop();
        sConfigServiceClient.terminate();
        return ERR_CONNECT;
    }
    sConnected = true;
    return 0;
}

int disconnectFromELogProcess() {
    if (!sConnected) {
        ELOG_ERROR("Cannot disconnect, not connected to ELog process");
        return 1;
    }

    if (!sConfigServiceClient.stop()) {
        ELOG_ERROR_EX(sLogger, "Failed to stop configuration service client");
        return ERR_STOP;
    }
    if (!sConfigServiceClient.terminate()) {
        ELOG_ERROR_EX(sLogger, "Failed to terminate configuration service client");
        return ERR_TERM;
    }
    sConnected = false;
    sLogSources.clear();
    return 0;
}

int queryLogLevel(const char* includeRegEx /* = ".*" */, const char* excludeRegEx /* = "" */) {
    if (!sConnected) {
        ELOG_ERROR("Cannot query log level, must connect first to ELog process");
        return 1;
    }
    std::unordered_map<std::string, elog::ELogLevel> logLevels;
    elog::ELogLevel reportLevel;
    if (!sConfigServiceClient.queryLogLevels(includeRegEx, excludeRegEx, logLevels, reportLevel)) {
        return ERR_QUERY;
    }
    for (const auto& pair : logLevels) {
        printf("%s: %s\n", pair.first.c_str(), elog::elogLevelToStr(pair.second));
    }
    printf("ELOG_REPORT_LEVEL = %s\n", elog::elogLevelToStr(reportLevel));
    fflush(stdout);
    return 0;
}

int updateLogLevels(const char* logLevelCfg) {
    if (!sConnected) {
        ELOG_ERROR("Cannot update log level, must connect first to ELog process");
        return 1;
    }

    // semicolon separated list without spaces of source=level*+-, with one additional
    // ELOG_REPORT_LEVEL=level

    // parse string into map
    std::vector<std::string> tokens;
    tokenize(logLevelCfg, tokens, " ");
    std::unordered_map<std::string, std::pair<elog::ELogLevel, elog::ELogPropagateMode>> logLevels;
    elog::ELogLevel reportLevel;
    bool hasReportLevel = false;

    // process tokens
    for (const std::string& token : tokens) {
        std::vector<std::string> subTokens;
        tokenize(token.c_str(), subTokens, "=");
        if (subTokens.size() != 2) {
            ELOG_ERROR_EX(sLogger, "Invalid log level update specification: %s", token.c_str());
            return ERR_INVALID_ARG;
        }
        if (subTokens[0].compare("ELOG_REPORT_LEVEL") == 0) {
            if (!elog::elogLevelFromStr(subTokens[1].c_str(), reportLevel)) {
                ELOG_ERROR_EX(sLogger, "Invalid report log level specification: %s",
                              subTokens[1].c_str());
                return ERR_INVALID_ARG;
            }
            hasReportLevel = true;
        } else {
            elog::ELogLevel logLevel;
            elog::ELogPropagateMode propagateMode;
            if (!parseLogLevel(subTokens[1].c_str(), logLevel, propagateMode)) {
                ELOG_ERROR_EX(sLogger, "Invalid log level specification: %s", token.c_str());
            }
            // override previous value
            logLevels[subTokens[0]] = std::make_pair(logLevel, propagateMode);
        }
    }

    // execute command
    int status = 0;
    std::string errorMsg;
    bool res = true;
    if (logLevels.empty() && !hasReportLevel) {
        ELOG_ERROR_EX(sLogger, "No valid input was parsed");
        return ERR_INVALID_ARG;
    }
    if (logLevels.empty()) {
        assert(hasReportLevel);
        res = sConfigServiceClient.updateReportLevel(reportLevel, status, errorMsg);
    } else {
        if (hasReportLevel) {
            res = sConfigServiceClient.updateLogReportLevels(logLevels, reportLevel, status,
                                                             errorMsg);
        } else {
            res = sConfigServiceClient.updateLogLevels(logLevels, status, errorMsg);
        }
    }
    if (!res) {
        return ERR_EXEC;
    } else if (status != 0) {
        ELOG_ERROR_EX(sLogger, "Command execution resulted in status %d: %s", status,
                      errorMsg.c_str());
        return ERR_EXEC;
    }
    return 0;
}

static bool execCommand(const std::string& cmd) {
    if (cmd.compare(CMD_EXIT) == 0 || cmd.compare("quit") == 0 || cmd.compare("q") == 0) {
        if (sConnected) {
            disconnectFromELogProcess();
        }
        return false;
    }
    printf("\n");
    if (cmd.compare(CMD_HELP) == 0) {
        printHelp();
    } else if (cmd.compare(CMD_LIST) == 0) {
        printServices();
    } else if (cmd.starts_with(CMD_CONNECT)) {
        std::string addr = trim(cmd.substr(strlen(CMD_CONNECT)));
        std::string host;
        int port;
        if (!parseHostPort(addr, host, port)) {
            ELOG_ERROR_EX(sLogger, "Invalid remote configuration service address: %s",
                          addr.c_str());
            return true;  // continue executing command
        }
        connectToELogProcess(host.c_str(), port);
    } else if (cmd.compare(CMD_DISCONNECT) == 0) {
        disconnectFromELogProcess();
    } else if (cmd.starts_with(CMD_QUERY_LOG_LEVEL)) {
        std::string logLevelQuery = trim(cmd.substr(strlen(CMD_QUERY_LOG_LEVEL)));
        std::vector<std::string> tokens;
        tokenize(logLevelQuery.c_str(), tokens);
        if (tokens.size() > 2) {
            ELOG_ERROR_EX(sLogger, "Too many arguments to query-log-level command: %s",
                          logLevelQuery.c_str());
            return true;  // continue executing command
        }
        if (tokens.size() == 0) {
            queryLogLevel();
        } else if (tokens.size() == 1) {
            queryLogLevel(tokens[0].c_str());
        } else {
            assert(tokens.size() == 2);
            queryLogLevel(tokens[0].c_str(), tokens[1].c_str());
        }
    } else if (cmd.starts_with(CMD_UPDATE_LOG_LEVEL)) {
        std::string logLevelCfg = trim(cmd.substr(strlen(CMD_UPDATE_LOG_LEVEL)));
        updateLogLevels(logLevelCfg.c_str());
    } else {
        ELOG_ERROR_EX(sLogger, "Unrecognized command: %s", cmd.c_str());
    }
    return true;
}

#ifndef ELOG_MSVC
void runCliLoop() {
    rl_attempted_completion_function = elog_cli_complete_func;
    rl_completion_entry_function = elog_cli_completion_entry_func;
    char* line = nullptr;
    printf("\n");
    while ((line = readline("<elog_cli> $ ")) != nullptr) {
        if (line == nullptr || *line == 0) {
            continue;
        }
        add_history(line);
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
        printf(ELOG_CLI_PROMPT);
        std::string strCmd;
        while (strCmd.empty()) {
            std::getline(std::cin, strCmd);
        }
        if (!execCommand(trim(strCmd))) {
            break;
        }
    }
}
#endif

#ifndef ELOG_MSVC
char** elog_cli_complete_func(const char* text, int start, int end) {
    // attempt completion only at start of line
    // if we are at start of line, then we give back command names
    if (start == 0) {
        return rl_completion_matches(text, elog_cli_cmd_generator_func);
    }

    // check text buffer, if it ends with connect command, then list hosts
    size_t cmdLen = 0;
#ifdef ELOG_CLI_HAS_SERVICE_DISCOVERY
    cmdLen = strlen(CMD_CONNECT);
    if (end == cmdLen + 1) {
        if (strncmp((char*)rl_line_buffer, CMD_CONNECT, cmdLen) == 0) {
            return rl_completion_matches(text, elog_cli_config_service_generator_func);
        }
    }
#endif

    // check text buffer, if it ends with query command, then list log sources
    cmdLen = strlen(CMD_QUERY_LOG_LEVEL);
    if (end == cmdLen + 1) {
        if (strncmp((char*)rl_line_buffer, CMD_QUERY_LOG_LEVEL, cmdLen) == 0) {
            return rl_completion_matches(text, elog_cli_log_source_generator_func);
        }
    }

    // check text buffer, if it ends with update command, then list log sources
    cmdLen = strlen(CMD_UPDATE_LOG_LEVEL);
    if (end == cmdLen + 1) {
        if (strncmp((char*)rl_line_buffer, CMD_UPDATE_LOG_LEVEL, cmdLen) == 0) {
            return rl_completion_matches(text, elog_cli_log_source_generator_func);
        }
    }

    return nullptr;
}

char* elog_cli_cmd_generator_func(const char* text, int state) {
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

char* elog_cli_completion_entry_func(const char* text, int state) {
    // this is a router function
    const char* lineBuffer = (const char*)rl_line_buffer;
#ifdef ELOG_CLI_HAS_SERVICE_DISCOVERY
    if (strncmp(lineBuffer, CMD_CONNECT, strlen(CMD_CONNECT)) == 0) {
        return elog_cli_config_service_generator_func(text, state);
    }
#endif
    if (strncmp(lineBuffer, CMD_QUERY_LOG_LEVEL, strlen(CMD_QUERY_LOG_LEVEL)) == 0) {
        return elog_cli_log_source_generator_func(text, state);
    }
    if (strncmp(lineBuffer, CMD_UPDATE_LOG_LEVEL, strlen(CMD_UPDATE_LOG_LEVEL)) == 0) {
        return elog_cli_log_source_generator_func(text, state);
    }

    return nullptr;
}

char* elog_cli_log_source_generator_func(const char* text, int state) {
    static int listIndex = 0;
    static int len = 0;

    if (!state) {
        listIndex = 0;
        len = strlen(text);
        if (!listAllLogSources()) {
            return nullptr;
        }
    }

    while (listIndex < sLogSources.size()) {
        const char* name = sLogSources[listIndex++].c_str();
        if (len == 0 || strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    return nullptr;
}

#ifdef ELOG_CLI_HAS_SERVICE_DISCOVERY
char* elog_cli_config_service_generator_func(const char* text, int state) {
    static int listIndex = 0;
    static int len = 0;

    if (!state) {
        listIndex = 0;
        len = strlen(text);
        if (listServices() != 0) {
            return nullptr;
        }
    }

    while (listIndex < sServiceList.size()) {
        const char* name = sServiceList[listIndex++].c_str();
        if (len == 0 || strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    return nullptr;
}
#endif
#endif

bool parseHostPort(const std::string& addr, std::string& host, int& port) {
    std::string::size_type colonPos = addr.find(':');
    if (colonPos == std::string::npos) {
        ELOG_ERROR_EX(sLogger, "Invalid server address, missing ':' between host and port: %s",
                      addr.c_str());
        return false;
    }
    host = addr.substr(0, colonPos);
    std::string portStr = addr.substr(colonPos + 1);
    port = std::strtol(portStr.c_str(), nullptr, 10);
    if (port == 0) {
        ELOG_ERROR_EX(sLogger, "Invalid server address, port '%s' is not a number: %s",
                      portStr.c_str(), addr.c_str());
        return false;
    }
    return true;
}

void tokenize(const char* str, std::vector<std::string>& tokens,
              const char* delims /* = " \t\r\n" */) {
    std::string s = str;
    std::string::size_type start = 0, end = 0;
    while ((start = s.find_first_not_of(delims, end)) != std::string::npos) {
        // start points to first non-delim char
        // now search for first delim char
        end = s.find_first_of(delims, start);
        tokens.push_back(s.substr(start, end - start));
    }
}

bool parseLogLevel(const char* logLevelStr, elog::ELogLevel& logLevel,
                   elog::ELogPropagateMode& propagateMode) {
    const char* ptr = nullptr;
    size_t parseLen = 0;
    if (!elog::elogLevelFromStr(logLevelStr, logLevel, &ptr, &parseLen)) {
        ELOG_ERROR_EX(sLogger, "Invalid log level: %s", logLevelStr);
        return false;
    }

    // parse optional propagation sign if there is any
    propagateMode = elog::ELogPropagateMode::PM_NONE;
    size_t len = strlen(logLevelStr);
    if (parseLen < len) {
        // there are more chars, only one is allowed
        if (parseLen + 1 != len) {
            ELOG_ERROR_EX(
                sLogger,
                "Invalid excess chars at global log level: %s (only one character is allowed: '*', "
                "'+' or '-')",
                logLevelStr);
            return false;
        } else if (*ptr == '*') {
            propagateMode = elog::ELogPropagateMode::PM_SET;
        } else if (*ptr == '-') {
            propagateMode = elog::ELogPropagateMode::PM_RESTRICT;
        } else if (*ptr == '+') {
            propagateMode = elog::ELogPropagateMode::PM_LOOSE;
        } else {
            ELOG_ERROR_EX(
                sLogger,
                "Invalid excess chars at global log level: %s (only one character is allowed: '*', "
                "'+' or '-')",
                logLevelStr);
            return false;
        }
    }

    return true;
}

bool listAllLogSources() {
    if (!sLogSources.empty()) {
        return true;
    }
    if (!sConnected) {
        return false;
    }

    std::unordered_map<std::string, elog::ELogLevel> logLevels;
    elog::ELogLevel reportLevel;
    if (!sConfigServiceClient.queryLogLevels(".*", "", logLevels, reportLevel)) {
        return false;
    }
    for (const auto& pair : logLevels) {
        sLogSources.push_back(pair.first);
    }
    sLogSources.push_back("ELOG_REPORT_LEVEL");
    return true;
}