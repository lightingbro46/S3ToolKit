#ifndef SQL_SQLMACRO_H
#define SQL_SQLMACRO_H

#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <memory>

namespace toolkit {

template<typename T>
class Optional {
    bool has_;
    T value_;

public:
    Optional() : has_(false) {}

    Optional(const T& val) : has_(true), value_(val) {}

    Optional(T&& val) : has_(true), value_(std::move(val)) {}

    Optional(const Optional& other) {
        has_ = other.has_;
        if (has_) value_ = other.value_;
    }

    Optional(Optional&& other) {
        has_ = other.has_;
        if (has_) value_ = std::move(other.value_);
    }

    Optional& operator=(const T& val) {
        has_ = true;
        value_ = val;
        return *this;
    }

    Optional& operator=(T&& val) {
        has_ = true;
        value_ = std::move(val);
        return *this;
    }

    Optional& operator=(const Optional& other) {
        if (this != &other) {
            has_ = other.has_;
            if (has_) value_ = other.value_;
        }
        return *this;
    }

    Optional& operator=(Optional&& other) {
        if (this != &other) {
            has_ = other.has_;
            if (has_) value_ = std::move(other.value_);
        }
        return *this;
    }

    bool hasValue() const { return has_; }

    explicit operator bool() const { return has_; }

    const T& value() const {
        if (!has_) throw std::runtime_error("Optional has no value");
        return value_;
    }

    T& value() {
        if (!has_) throw std::runtime_error("Optional has no value");
        return value_;
    }

    void reset() { has_ = false; }
};

class SqlValue {
public:
    enum Type {
        TYPE_NULL,
        TYPE_INT,
        TYPE_DOUBLE,
        TYPE_STRING
    };

    SqlValue() : type_(TYPE_NULL), intVal_(0), doubleVal_(0.0) {}
    SqlValue(int v) : type_(TYPE_INT), intVal_(v), doubleVal_(0.0) {}
    SqlValue(double v) : type_(TYPE_DOUBLE), intVal_(0), doubleVal_(v) {}
    SqlValue(const std::string& v) : type_(TYPE_STRING), strVal_(v), intVal_(0), doubleVal_(0.0) {}
    SqlValue(const char* v) : type_(TYPE_STRING), strVal_(v), intVal_(0), doubleVal_(0.0) {}

    template<typename T>
    SqlValue(const Optional<T>& opt) {
        if (!opt.has_value()) {
            type_ = TYPE_NULL;
        } else {
            *this = SqlValue(opt.value());
        }
    }

    Type type() const { return type_; }

    std::string toString() const {
        switch (type_) {
            case TYPE_INT: return std::to_string(intVal_);
            case TYPE_DOUBLE: return std::to_string(doubleVal_);
            case TYPE_STRING: return strVal_;
            case TYPE_NULL: default: return "NULL";
        }
    }

    int asInt() const {
        if (type_ == TYPE_INT) return intVal_;
        if (type_ == TYPE_DOUBLE) return static_cast<int>(doubleVal_);
        if (type_ == TYPE_STRING) return std::stoi(strVal_);
        return 0;
    }

    double asDouble() const {
        if (type_ == TYPE_DOUBLE) return doubleVal_;
        if (type_ == TYPE_INT) return intVal_;
        if (type_ == TYPE_STRING) return std::stod(strVal_);
        return 0.0;
    }

    std::string asString() const {
        return toString();
    }

    bool isNull() const { return type_ == TYPE_NULL; }

    template<typename T>
    Optional<T> asOptional() const {
        return isNull() ? Optional<T>() : Optional<T>(static_cast<T>(...));
    }

private:
    Type type_;
    int intVal_;
    double doubleVal_;
    std::string strVal_;
};

} // namespace toolkit

#define SQL_FIELD(name) std::make_pair(#name, toolkit::SqlValue(obj.name))
#define SQL_FIELD_OPT(name) \
    (obj.name.has_value() ? std::make_pair(#name, toolkit::SqlValue(obj.name.value())) : std::make_pair(#name, toolkit::SqlValue()))

#define SQL_CLASS(className, tableName, dbName, ...)                                                      \
    static std::string tableName() { return tableName; }                                                \
    static std::string dbName() { return dbName; }                                                      \
    static std::vector<std::string> getColumnNames()                                                    \
    {                                                                                                   \
        return splitColumnNames(#__VA_ARGS__);                                                          \
    }                                                                                                   \
    static std::vector<std::pair<std::string, toolkit::SqlValue>> toKeyValuePairs(const className &obj) \
    {                                                                                                   \
        std::vector<std::pair<std::string, toolkit::SqlValue>> kvs;                                     \
        applyKeyValuePairs(kvs, obj, __VA_ARGS__);                                                      \
        return kvs;                                                                                     \
    }                                                                                                   \
    static className fromMap(const std::map<std::string, std::string> &m)                               \
    {                                                                                                   \
        className obj;                                                                                  \
        applyFromMap(obj, m, __VA_ARGS__);                                                              \
        return obj;                                                                                     \
    }

#define DEFINE_SPLITTER                                                              \
    static std::vector<std::string> splitColumnNames(const std::string &s)           \
    {                                                                                \
        std::vector<std::string> result;                                             \
        size_t start = 0, end = 0;                                                   \
        while ((end = s.find(',', start)) != std::string::npos)                      \
        {                                                                            \
            result.push_back(trim(s.substr(start, end - start)));                    \
            start = end + 1;                                                         \
        }                                                                            \
        result.push_back(trim(s.substr(start)));                                     \
        return result;                                                               \
    }                                                                                \
    static std::string trim(const std::string &s)                                    \
    {                                                                                \
        const char *ws = " \t\n\r";                                                  \
        size_t start = s.find_first_not_of(ws);                                      \
        size_t end = s.find_last_not_of(ws);                                         \
        return (start == std::string::npos) ? "" : s.substr(start, end - start + 1); \
    }

#define APPLY_KV_PAIR(obj, name) kvs.push_back(SQL_FIELD(name));
#define APPLY_KV_PAIR_OPT(obj, name) kvs.push_back(SQL_FIELD_OPT(name));

#define applyKeyValuePairs(kvs, obj, ...) \
    applyKeyValuePairsImpl(kvs, obj, __VA_ARGS__);

#define applyFromMap(obj, m, ...) \
    applyFromMapImpl(obj, m, __VA_ARGS__);

// Variadic templates to walk through fields
#define EXPAND(...) __VA_ARGS__

#define FIELD_APPLIER(obj, m, field)                              \
    if (m.count(#field))                                          \
    {                                                             \
        obj.field = convertTo<decltype(obj.field)>(m.at(#field)); \
    }

#define FIELD_APPLIER_OPT(obj, m, field)                                                                                          \
    if (m.count(#field))                                                                                                          \
    {                                                                                                                             \
        obj.field = Optional<decltype(obj.field)::value_type>(convertTo<typename decltype(obj.field)::value_type>(m.at(#field))); \
    }

// Convert string to types
inline int convertToInt(const std::string& s) { return std::stoi(s); }
inline double convertToDouble(const std::string& s) { return std::stod(s); }
inline float convertToFloat(const std::string& s) { return std::stof(s); }
inline bool convertToBool(const std::string& s) { return s == "1" || s == "true"; }
inline std::string convertToString(const std::string& s) { return s; }

// Generic convertTo template
template<typename T>
T convertTo(const std::string& s);

template<> inline int convertTo<int>(const std::string& s) { return convertToInt(s); }
template<> inline double convertTo<double>(const std::string& s) { return convertToDouble(s); }
template<> inline float convertTo<float>(const std::string& s) { return convertToFloat(s); }
template<> inline bool convertTo<bool>(const std::string& s) { return convertToBool(s); }
template<> inline std::string convertTo<std::string>(const std::string& s) { return convertToString(s); }

// Actual field logic handlers
#define applyKeyValuePairsImpl(kvs, obj, ...)                                                                                                                                                                               \
    int unpack[] = {0, ((obj.__VA_ARGS__.has_value() ? kvs.push_back(std::make_pair(#__VA_ARGS__, toolkit::SqlValue(obj.__VA_ARGS__.value()))) : kvs.push_back(std::make_pair(#__VA_ARGS__, toolkit::SqlValue()))), 0)...}; \
    (void)unpack;

#define applyFromMapImpl(obj, m, ...)                                                                                                                                                                                         \
    int unpack[] = {0, ((m.count(#__VA_ARGS__) ? obj.__VA_ARGS__ = Optional<typename decltype(obj.__VA_ARGS__)::value_type>(convertTo<typename decltype(obj.__VA_ARGS__)::value_type>(m.at(#__VA_ARGS__))) : void()), 0)...}; \
    (void)unpack;

#endif // SQL_SQLMACRO_H