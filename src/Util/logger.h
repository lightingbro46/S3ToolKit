#ifndef UTIL_LOGGER_H_
#define UTIL_LOGGER_H_

#include <cstdarg>
#include <set>
#include <map>
#include <fstream>
#include <thread>
#include <memory>
#include <mutex>
#include "util.h"
#include "List.h"
#include "Thread/semaphore.h"

namespace toolkit {

class LogContext;
class LogChannel;
class LogWriter;
class Logger;

using LogContextPtr = std::shared_ptr<LogContext>;

typedef enum {
    LTrace = 0, LDebug, LInfo, LWarn, LError
} LogLevel;

Logger &getLogger();
void setLogger(Logger *logger);

/**
 * Log class
*/
class Logger : public std::enable_shared_from_this<Logger>, public noncopyable {
public:
    friend class AsyncLogWriter;
    using Ptr = std::shared_ptr<Logger>;

    /**
     * Get log singleton
     * @return
     */
    static Logger &Instance();

    explicit Logger(const std::string &loggerName);
    ~Logger();

    /**
     * Add log channel, not thread-safe
     * @param channel log channel
     */
    void add(const std::shared_ptr<LogChannel> &channel);

    /**
     * Delete log channel, not thread-safe
     * @param name log channel name
     */
    void del(const std::string &name);

    /**
     * Get log channel, not thread-safe
     * @param name log channel name
     * @return log channel
     */
    std::shared_ptr<LogChannel> get(const std::string &name);

    /**
     * Set log writer, not thread-safe
     * @param writer log writer
     */
    void setWriter(const std::shared_ptr<LogWriter> &writer);

    /**
     * Set log level for all log channels
     * @param level log level
     */
    void setLevel(LogLevel level);

    /**
     * Get logger name
     * @return logger name
     */
    const std::string &getName() const;

    /**
     * Write log
     * @param ctx log information
     */
    void write(const LogContextPtr &ctx);

private:
    /**
     * Write log to each channel, only for AsyncLogWriter to call
     * @param ctx log information
     */
    void writeChannels(const LogContextPtr &ctx);
    void writeChannels_l(const LogContextPtr &ctx);

private:
    LogContextPtr _last_log;
    std::string _logger_name;
    std::shared_ptr<LogWriter> _writer;
    std::shared_ptr<LogChannel> _default_channel;
    std::map<std::string, std::shared_ptr<LogChannel> > _channels;
};

///////////////////LogContext///////////////////
/**
 * Log Context
*/
class LogContext : public std::ostringstream {
public:
    //_file,_function changed to string to save, the purpose is that in some cases, the pointer may become invalid
    //For example, a log is printed in a dynamic library, and then the dynamic library is unloaded, so the pointer to the static data area will become invalid
    LogContext() = default;
    LogContext(LogLevel level, const char *file, const char *function, int line, const char *module_name, const char *flag);
    ~LogContext() = default;

    LogLevel _level;
    int _line;
    int _repeat = 0;
    std::string _file;
    std::string _function;
    std::string _thread_name;
    std::string _module_name;
    std::string _flag;
    struct timeval _tv;

    const std::string &str();

private:
    bool _got_content = false;
    std::string _content;
};

/**
 * Log Context Capturer
 */
class LogContextCapture {
public:
    using Ptr = std::shared_ptr<LogContextCapture>;

    LogContextCapture(Logger &logger, LogLevel level, const char *file, const char *function, int line, const char *flag = "");
    LogContextCapture(const LogContextCapture &that);
    ~LogContextCapture();

    /**
     * Input std::endl (newline character) to output log immediately
     * @param f std::endl (newline character)
     * @return Self-reference
     */
    LogContextCapture &operator<<(std::ostream &(*f)(std::ostream &));

    template<typename T>
    LogContextCapture &operator<<(T &&data) {
        if (!_ctx) {
            return *this;
        }
        (*_ctx) << std::forward<T>(data);
        return *this;
    }

    void clear();

private:
    LogContextPtr _ctx;
    Logger &_logger;
};


///////////////////LogWriter///////////////////
/**
 * Log Writer
 */
class LogWriter : public noncopyable {
public:
    LogWriter() = default;
    virtual ~LogWriter() = default;

    virtual void write(const LogContextPtr &ctx, Logger &logger) = 0;
};

class AsyncLogWriter : public LogWriter {
public:
    AsyncLogWriter();
    ~AsyncLogWriter();

private:
    void run();
    void flushAll();
    void write(const LogContextPtr &ctx, Logger &logger) override;

private:
    bool _exit_flag;
    semaphore _sem;
    std::mutex _mutex;
    std::shared_ptr<std::thread> _thread;
    List<std::pair<LogContextPtr, Logger *> > _pending;
};

///////////////////LogChannel///////////////////
/**
 * Log Channel
 */
class LogChannel : public noncopyable {
public:
    LogChannel(const std::string &name, LogLevel level = LTrace);
    virtual ~LogChannel();

    virtual void write(const Logger &logger, const LogContextPtr &ctx) = 0;
    const std::string &name() const;
    void setLevel(LogLevel level);
    static std::string printTime(const timeval &tv);

protected:
    /**
     * Print log to output stream
     * @param ost Output stream
     * @param enable_color Whether to enable color
     * @param enable_detail Whether to print details (function name, source file name, source line)
    */
    virtual void format(const Logger &logger, std::ostream &ost, const LogContextPtr &ctx, bool enable_color = true, bool enable_detail = true);

protected:
    std::string _name;
    LogLevel _level;
};

/**
 * Output log to broadcast
 */
class EventChannel : public LogChannel {
public:
    //Broadcast name when outputting log
    static const std::string kBroadcastLogEvent;
    //The toolkit currently only has one global variable referenced externally, reducing the export of related definitions, and exporting the following functions to avoid exporting kBroadcastLogEvent
    static const std::string& getBroadcastLogEventName();
    //Log broadcast parameter type and list
    #define BroadcastLogEventArgs const Logger &logger, const LogContextPtr &ctx

    EventChannel(const std::string &name = "EventChannel", LogLevel level = LTrace);
    ~EventChannel() override = default;

    void write(const Logger &logger, const LogContextPtr &ctx) override;
};

/**
 * Output logs to the terminal, supporting output to Android logcat
 */
class ConsoleChannel : public LogChannel {
public:
    ConsoleChannel(const std::string &name = "ConsoleChannel", LogLevel level = LTrace);
    ~ConsoleChannel() override = default;

    void write(const Logger &logger, const LogContextPtr &logContext) override;
};

/**
 * Output logs to a file
 */
class FileChannelBase : public LogChannel {
public:
    FileChannelBase(const std::string &name = "FileChannelBase", const std::string &path = exePath() + ".log", LogLevel level = LTrace);
    ~FileChannelBase() override;

    void write(const Logger &logger, const LogContextPtr &ctx) override;
    bool setPath(const std::string &path);
    const std::string &path() const;

protected:
    virtual bool open();
    virtual void close();
    virtual size_t size();

protected:
    std::string _path;
    std::ofstream _fstream;
};

class Ticker;

/**
 * Auto-cleaning log file channel
 * Default to keep logs for up to 30 days
 */
class FileChannel : public FileChannelBase {
public:
    FileChannel(const std::string &name = "FileChannel", const std::string &dir = exeDir() + "log/", LogLevel level = LTrace);
    ~FileChannel() override = default;

    /**
     * Trigger new log file creation or deletion of old log files when writing logs
     * @param logger
     * @param stream
     */
    void write(const Logger &logger, const LogContextPtr &ctx) override;

    /**
     * Set the maximum number of days to keep logs
     * @param max_day Number of days
     */
    void setMaxDay(size_t max_day);

    /**
     * Set the maximum size of log slice files
     * @param max_size Unit: MB
     */
    void setFileMaxSize(size_t max_size);

    /**
     * Set the maximum number of log slice files
     * @param max_count Number of files
     */
    void setFileMaxCount(size_t max_count);

private:
    /**
     * Delete log slice files, conditions are exceeding the maximum number of days and slices
     */
    void clean();

    /**
     * Check the current log slice file size, if exceeded the limit, create a new log slice file
     */
    void checkSize(time_t second);

    /**
     * Create and switch to the next log slice file
     */
    void changeFile(time_t second);

private:
    bool _can_write = false;
    //Default to keep log files for up to 30 days
    size_t _log_max_day = 30;
    //Maximum default size of each log slice file is 128MB
    size_t _log_max_size = 128;
    //Default to keep up to 30 log slice files
    size_t _log_max_count = 30;
    //Current log slice file index
    size_t _index = 0;
    int64_t _last_day = -1;
    time_t _last_check_time = 0;
    std::string _dir;
    std::set<std::string> _log_file_map;
};

#if defined(__MACH__) || ((defined(__linux) || defined(__linux__)) && !defined(ANDROID))
class SysLogChannel : public LogChannel {
public:
    SysLogChannel(const std::string &name = "SysLogChannel", LogLevel level = LTrace);
    ~SysLogChannel() override = default;

    void write(const Logger &logger, const LogContextPtr &logContext) override;
};

#endif//#if defined(__MACH__) || ((defined(__linux) || defined(__linux__)) &&  !defined(ANDROID))

class BaseLogFlagInterface {
protected:
    virtual ~BaseLogFlagInterface() {}
    //Get log flag
    const char* getLogFlag(){
        return _log_flag;
    }
    void setLogFlag(const char *flag) { _log_flag = flag; }
private:
    const char *_log_flag = "";
};

class LoggerWrapper {
public:
    template<typename First, typename ...ARGS>
    static inline void printLogArray(Logger &logger, LogLevel level, const char *file, const char *function, int line, First &&first, ARGS &&...args) {
        LogContextCapture log(logger, level, file, function, line);
        log << std::forward<First>(first);
        appendLog(log, std::forward<ARGS>(args)...);
    }

    static inline void printLogArray(Logger &logger, LogLevel level, const char *file, const char *function, int line) {
        LogContextCapture log(logger, level, file, function, line);
    }

    template<typename Log, typename First, typename ...ARGS>
    static inline void appendLog(Log &out, First &&first, ARGS &&...args) {
        out << std::forward<First>(first);
        appendLog(out, std::forward<ARGS>(args)...);
    }

    template<typename Log>
    static inline void appendLog(Log &out) {}

    //printf style log print
    static void printLog(Logger &logger, int level, const char *file, const char *function, int line, const char *fmt, ...);
    static void printLogV(Logger &logger, int level, const char *file, const char *function, int line, const char *fmt, va_list ap);
};

//Can reset default value
extern Logger *g_defaultLogger;

//Usage: DebugL << 1 << "+" << 2 << '=' << 3;
#define WriteL(level) ::toolkit::LogContextCapture(::toolkit::getLogger(), level, __FILE__, __FUNCTION__, __LINE__)
#define TraceL WriteL(::toolkit::LTrace)
#define DebugL WriteL(::toolkit::LDebug)
#define InfoL WriteL(::toolkit::LInfo)
#define WarnL WriteL(::toolkit::LWarn)
#define ErrorL WriteL(::toolkit::LError)

//Can only be used in classes that virtually inherit from BaseLogFlagInterface
#define WriteF(level) ::toolkit::LogContextCapture(::toolkit::getLogger(), level, __FILE__, __FUNCTION__, __LINE__, getLogFlag())
#define TraceF WriteF(::toolkit::LTrace)
#define DebugF WriteF(::toolkit::LDebug)
#define InfoF WriteF(::toolkit::LInfo)
#define WarnF WriteF(::toolkit::LWarn)
#define ErrorF WriteF(::toolkit::LError)

//Usage: PrintD("%d + %s = %c", 1, "2", 'c');
#define PrintLog(level, ...) ::toolkit::LoggerWrapper::printLog(::toolkit::getLogger(), level, __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define PrintT(...) PrintLog(::toolkit::LTrace, ##__VA_ARGS__)
#define PrintD(...) PrintLog(::toolkit::LDebug, ##__VA_ARGS__)
#define PrintI(...) PrintLog(::toolkit::LInfo, ##__VA_ARGS__)
#define PrintW(...) PrintLog(::toolkit::LWarn, ##__VA_ARGS__)
#define PrintE(...) PrintLog(::toolkit::LError, ##__VA_ARGS__)

//Usage: LogD(1, "+", "2", '=', 3);
//Used for template instantiation, because if the number and type of print parameters are inconsistent each time, it may cause binary code bloat
#define LogL(level, ...) ::toolkit::LoggerWrapper::printLogArray(::toolkit::getLogger(), (LogLevel)level, __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LogT(...) LogL(::toolkit::LTrace, ##__VA_ARGS__)
#define LogD(...) LogL(::toolkit::LDebug, ##__VA_ARGS__)
#define LogI(...) LogL(::toolkit::LInfo, ##__VA_ARGS__)
#define LogW(...) LogL(::toolkit::LWarn, ##__VA_ARGS__)
#define LogE(...) LogL(::toolkit::LError, ##__VA_ARGS__)

} /* namespace toolkit */
#endif /* UTIL_LOGGER_H_ */
