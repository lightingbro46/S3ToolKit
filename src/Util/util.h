#ifndef UTIL_UTIL_H_
#define UTIL_UTIL_H_

#include <ctime>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <sstream>
#include <vector>
#include <atomic>
#include <unordered_map>
#include "function_traits.h"
#include "onceToken.h"
#if defined(_WIN32)
#undef FD_SETSIZE
//Modify the default 64 to 1024 paths
#define FD_SETSIZE 1024
#include <winsock2.h>
#pragma comment (lib,"WS2_32")
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <cstddef>
#endif // defined(_WIN32)

#if defined(__APPLE__)
#include "TargetConditionals.h"
#if TARGET_IPHONE_SIMULATOR
#define OS_IPHONE
#elif TARGET_OS_IPHONE
#define OS_IPHONE
#endif
#endif //__APPLE__

#define INSTANCE_IMP(class_name, ...) \
class_name &class_name::Instance() { \
    static std::shared_ptr<class_name> s_instance(new class_name(__VA_ARGS__)); \
    static class_name &s_instance_ref = *s_instance; \
    return s_instance_ref; \
}

namespace toolkit {

#define StrPrinter ::toolkit::_StrPrinter()
class _StrPrinter : public std::string {
public:
    _StrPrinter() {}

    template<typename T>
    _StrPrinter& operator <<(T && data) {
        _stream << std::forward<T>(data);
        this->std::string::operator=(_stream.str());
        return *this;
    }

    std::string operator <<(std::ostream&(*f)(std::ostream&)) const {
        return *this;
    }

private:
    std::stringstream _stream;
};

//Prohibit copying of base classes
class noncopyable {
protected:
    noncopyable() {}
    ~noncopyable() {}
private:
    //Prohibit copying
    noncopyable(const noncopyable &that) = delete;
    noncopyable(noncopyable &&that) = delete;
    noncopyable &operator=(const noncopyable &that) = delete;
    noncopyable &operator=(noncopyable &&that) = delete;
};

#ifndef CLASS_FUNC_TRAITS
#define CLASS_FUNC_TRAITS(func_name) \
template<typename T, typename ... ARGS> \
constexpr bool Has_##func_name(decltype(&T::on##func_name) /*unused*/) { \
    using RET = typename function_traits<decltype(&T::on##func_name)>::return_type; \
    using FuncType = RET (T::*)(ARGS...);   \
    return std::is_same<decltype(&T::on ## func_name), FuncType>::value; \
} \
\
template<class T, typename ... ARGS> \
constexpr bool Has_##func_name(...) { \
    return false; \
} \
\
template<typename T, typename ... ARGS> \
static void InvokeFunc_##func_name(typename std::enable_if<!Has_##func_name<T, ARGS...>(nullptr), T>::type &obj, ARGS ...args) {} \
\
template<typename T, typename ... ARGS>\
static typename function_traits<decltype(&T::on##func_name)>::return_type InvokeFunc_##func_name(typename std::enable_if<Has_##func_name<T, ARGS...>(nullptr), T>::type &obj, ARGS ...args) {\
    return obj.on##func_name(std::forward<ARGS>(args)...);\
}
#endif //CLASS_FUNC_TRAITS

#ifndef CLASS_FUNC_INVOKE
#define CLASS_FUNC_INVOKE(T, obj, func_name, ...) InvokeFunc_##func_name<T>(obj, ##__VA_ARGS__)
#endif //CLASS_FUNC_INVOKE

CLASS_FUNC_TRAITS(Destory)
CLASS_FUNC_TRAITS(Create)

/**
 * Object-safe construction and destruction, execute the onCreate function after construction, and execute the onDestroy function before destruction
 * Methods that cannot be called during construction or destruction, such as shared_from_this or virtual functions, can be executed in the onCreate and onDestroy functions
 * @warning The onDestroy function must have 0 parameters; otherwise, it will be ignored
 */
class Creator {
public:
    /**
     * Create an object, execute onCreate and onDestroy functions with empty parameters
     * @param args List of parameters for the object's constructor
     * @return Smart pointer to the args object
     */
    template<typename C, typename ...ArgsType>
    static std::shared_ptr<C> create(ArgsType &&...args) {
        std::shared_ptr<C> ret(new C(std::forward<ArgsType>(args)...), [](C *ptr) {
            try {
                CLASS_FUNC_INVOKE(C, *ptr, Destory);
            } catch (std::exception &ex){
                onDestoryException(typeid(C), ex);
            }
            delete ptr;
        });
        CLASS_FUNC_INVOKE(C, *ret, Create);
        return ret;
    }

    /**
     * Create an object, execute the onCreate function with specified parameters
     * @param args List of parameters for the object's onCreate function
     * @warning The type and number of args parameters must match the type of the onCreate function (default parameters cannot be ignored), otherwise it will be ignored due to template matching failure
     * @return Smart pointer to the args object
     */
    template<typename C, typename ...ArgsType>
    static std::shared_ptr<C> create2(ArgsType &&...args) {
        std::shared_ptr<C> ret(new C(), [](C *ptr) {
            try {
                CLASS_FUNC_INVOKE(C, *ptr, Destory);
            } catch (std::exception &ex){
                onDestoryException(typeid(C), ex);
            }
            delete ptr;
        });
        CLASS_FUNC_INVOKE(C, *ret, Create, std::forward<ArgsType>(args)...);
        return ret;
    }

private:
    static void onDestoryException(const std::type_info &info, const std::exception &ex);

private:
    Creator() = default;
    ~Creator() = default;
};

template <class C>
class ObjectStatistic{
public:
    ObjectStatistic(){
        ++getCounter();
    }

    ~ObjectStatistic(){
        --getCounter();
    }

    static size_t count(){
        return getCounter().load();
    }

private:
    static std::atomic<size_t> & getCounter();
};

#define StatisticImp(Type)  \
    template<> \
    std::atomic<size_t>& ObjectStatistic<Type>::getCounter(){ \
        static std::atomic<size_t> instance(0); \
        return instance; \
    }

class AssertFailedException : public std::runtime_error {
public:
    template<typename ...T>
    AssertFailedException(T && ...args) : std::runtime_error(std::forward<T>(args)...) {}
};

std::string makeRandStr(int sz, bool printable = true);
uint64_t makeRandNum();
std::string makeUuidStr();
std::string hexdump(const void *buf, size_t len);
std::string hexmem(const void* buf, size_t len);
std::string exePath(bool isExe = true);
std::string exeDir(bool isExe = true);
std::string exeName(bool isExe = true);

std::vector<std::string> split(const std::string& s, const char *delim);
//Remove leading and trailing spaces, line breaks, tabs...
std::string& trim(std::string &s,const std::string &chars=" \r\n\t");
std::string trim(std::string &&s,const std::string &chars=" \r\n\t");
//Convert string to lowercase
std::string &strToLower(std::string &str);
std::string strToLower(std::string &&str);
//Convert string to uppercase
std::string &strToUpper(std::string &str);
std::string strToUpper(std::string &&str);
//Replace substring
void replace(std::string &str, const std::string &old_str, const std::string &new_str, std::string::size_type b_pos = 0) ;
//Determine if it's an IP
bool isIP(const char *str);
//Check if a string starts with xx
bool start_with(const std::string &str, const std::string &substr);
//Check if a string ends with xx
bool end_with(const std::string &str, const std::string &substr);
//Concatenate format string
template<typename... Args>
std::string str_format(const std::string &format, Args... args) {

    // Calculate the buffer size
    auto size_buf = snprintf(nullptr, 0, format.c_str(), args ...) + 1;
    // Allocate the buffer
#if __cplusplus >= 201703L
    // C++17
    auto buf = std::make_unique<char[]>(size_buf);
#else
    // C++11
    std:: unique_ptr<char[]> buf(new(std::nothrow) char[size_buf]);
#endif
    // Check if the allocation is successful
    if (buf == nullptr) {
        return {};
    }
    // Fill the buffer with formatted string
    auto result = snprintf(buf.get(), size_buf, format.c_str(), args ...);
    // Return the formatted string
    return std::string(buf.get(), buf.get() + result);
}

#ifndef bzero
#define bzero(ptr,size)  memset((ptr),0,(size));
#endif //bzero

#if defined(ANDROID)
template <typename T>
std::string to_string(T value){
    std::ostringstream os ;
    os <<  std::forward<T>(value);
    return os.str() ;
}
#endif//ANDROID

#if defined(_WIN32)
int gettimeofday(struct timeval *tp, void *tzp);
void usleep(int micro_seconds);
void sleep(int second);
int vasprintf(char **strp, const char *fmt, va_list ap);
int asprintf(char **strp, const char *fmt, ...);
const char *strcasestr(const char *big, const char *little);

#if !defined(strcasecmp)
    #define strcasecmp _stricmp
#endif

#if !defined(strncasecmp)
#define strncasecmp _strnicmp
#endif

#ifndef ssize_t
    #ifdef _WIN64
        #define ssize_t int64_t
    #else
        #define ssize_t int32_t
    #endif
#endif
#endif //WIN32

/**
 * Get time difference, return value in seconds
 */
long getGMTOff();

/**
 * Get the number of milliseconds since 1970
 * @param system_time Whether it's system time (system time can be rolled back), otherwise it's program startup time (cannot be rolled back)
 */
uint64_t getCurrentMillisecond(bool system_time = false);

/**
 * Get the number of microseconds since 1970
 * @param system_time Whether it's system time (system time can be rolled back), otherwise it's program startup time (cannot be rolled back)
 */
uint64_t getCurrentMicrosecond(bool system_time = false);

/**
 * Get time string
 * @param fmt Time format, e.g. %Y-%m-%d %H:%M:%S
 * @return Time string
 */
std::string getTimeStr(const char *fmt,time_t time = 0);

/**
 * Get local time based on Unix timestamp
 * @param sec Unix timestamp
 * @return tm structure
 */
struct tm getLocalTime(time_t sec);

/**
 * Set thread name
 */
void setThreadName(const char *name);

/**
 * Get thread name
 */
std::string getThreadName();

/**
 * Set current thread CPU affinity
 * @param i CPU index, if -1, cancel CPU affinity
 * @return Whether successful, currently only supports Linux
 */
bool setThreadAffinity(int i);

/**
 * Get class name based on typeid(class).name()
 */
std::string demangle(const char *mangled);

/**
 * Get environment variable content, starting with '$'
 */
std::string getEnv(const std::string &key);

//Can store any object
class Any {
public:
    using Ptr = std::shared_ptr<Any>;

    Any() = default;
    ~Any() = default;

    Any(const Any &that) = default;
    Any(Any &&that) {
        _type = that._type;
        _data = std::move(that._data);
    }

    Any &operator=(const Any &that) = default;
    Any &operator=(Any &&that) {
        _type = that._type;
        _data = std::move(that._data);
        return *this;
    }

    template <typename T, typename... ArgsType>
    static Any make(ArgsType &&...args) {
        Any ret;
        ret.set<T>(std::forward<ArgsType>(args)...);
        return ret;
    }

    template <typename T>
    Any(std::shared_ptr<T> data) {
        set<T>(std::move(data));
    }

    template <typename T, typename... ArgsType>
    void set(ArgsType &&...args) {
        _type = &typeid(T);
        _data.reset(new T(std::forward<ArgsType>(args)...), [](void *ptr) { delete (T *)ptr; });
    }

    template <typename T>
    void set(std::shared_ptr<T> data) {
        if (data) {
            _type = &typeid(T);
            _data = std::move(data);
        } else {
            reset();
        }
    }

    template <typename T>
    T &get(bool safe = true) {
        if (!_data) {
            throw std::invalid_argument("Any is empty");
        }
        if (safe && !is<T>()) {
            throw std::invalid_argument("Any::get(): " + demangle(_type->name()) + " unable cast to " + demangle(typeid(T).name()));
        }
        return *((T *)_data.get());
    }

    void *get() { return _data.get(); }

    template <typename T>
    const T &get(bool safe = true) const {
        return const_cast<Any &>(*this).get<T>(safe);
    }

    template <typename T>
    std::shared_ptr<T> get_shared(bool safe = true) {
        if (!_data) {
            throw std::invalid_argument("Any is empty");
        }
        if (safe && !is<T>()) {
            throw std::invalid_argument("Any::get(): " + demangle(_type->name()) + " unable cast to " + demangle(typeid(T).name()));
        }
        return std::static_pointer_cast<T>(_data);
    }

    template <typename T>
    bool is() const {
        return _type && typeid(T) == *_type;
    }

    operator bool() const { return _data.operator bool(); }

    bool empty() const { return !bool(); }

    void reset() {
        _type = nullptr;
        _data = nullptr;
    }

    Any &operator=(std::nullptr_t) {
        reset();
        return *this;
    }

    std::string type_name() const {
        if (!_type) {
            return "";
        }
        return demangle(_type->name());
    }

private:
    const std::type_info* _type = nullptr;
    std::shared_ptr<void> _data;
};

//Used to store some additional properties
class AnyStorage : public std::unordered_map<std::string, Any> {
public:
    AnyStorage() = default;
    ~AnyStorage() = default;
    using Ptr = std::shared_ptr<AnyStorage>;
};

template <class R, class... ArgTypes>
class function_safe;

template <typename R, typename... ArgTypes>
class function_safe<R(ArgTypes...)> {
public:
    using func = std::function<R(ArgTypes...)>;
    using this_type = function_safe<R(ArgTypes...)>;

    template <class F>
    using enable_if_not_this = typename std::enable_if<!std::is_same<this_type, typename std::decay<F>::type>::value, typename std::decay<F>::type>::type;

    R operator()(ArgTypes... args) const {
        onceToken token([&]() { _doing = true; checkUpdate(); }, [&]() { checkUpdate(); _doing = false; });
        if (!_impl) {
            throw std::invalid_argument("try to invoke a empty functional");
        }
        return _impl(std::forward<ArgTypes>(args)...);
    }

    function_safe(std::nullptr_t) {
        update(func{});
    }

    function_safe &operator=(std::nullptr_t) {
        update(func{});
        return *this;
    }

    template <typename F, typename = enable_if_not_this<F>>
    function_safe(F &&f) {
        update(func { std::forward<F>(f) });
    }

    template <typename F, typename = enable_if_not_this<F>>
    this_type &operator=(F &&f) {
        update(func { std::forward<F>(f) });
        return *this;
    }

    function_safe() = default;
    function_safe(this_type &&) = default;
    function_safe(const this_type &) = default;
    this_type &operator=(this_type &&) = default;
    this_type &operator=(const this_type &) = default;

    operator bool() const { return _update ? (bool)_tmp : (bool)_impl; }

private:
    void checkUpdate() const {
        if (_update) {
            _update = false;
            _impl = std::move(_tmp);
        }
    }
    void update(func in) {
        if (!_doing) {
            // Not in execution, then overwrite immediately
            _impl = std::move(in);
            _tmp = nullptr;
            _update = false;
        } else {
            // Execution, delay overwrite
            _tmp = std::move(in);
            _update = true;
        }
    }

private:
    mutable bool _update = false;
    mutable bool _doing = false;
    mutable func _tmp;
    mutable func _impl;
};

/**
 * Format by uuid/guid form
 */
std::string format_guid(const std::string &s);

/**
 * Generate uuid/guid
 */
std::string generate_guid();

}  // namespace toolkit

#ifdef __cplusplus
extern "C" {
#endif
extern void Assert_Throw(int failed, const char *exp, const char *func, const char *file, int line, const char *str);
#ifdef __cplusplus
}
#endif

#endif /* UTIL_UTIL_H_ */
