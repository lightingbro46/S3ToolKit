#ifndef SQL_SQLMACRO_H
#define SQL_SQLMACRO_H

#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <memory>

namespace toolkit {

class SqlValue {
public:
    enum Type { INT, DOUBLE, STRING, NULLTYPE };

    SqlValue() : _type(NULLTYPE) {}
    SqlValue(int i) : _type(INT), _intVal(i) {}
    SqlValue(double d) : _type(DOUBLE), _doubleVal(d) {}
    SqlValue(const std::string& s) : _type(STRING), _strVal(new std::string(s)) {}
    SqlValue(const char* s) : _type(STRING), _strVal(new std::string(s)) {}
    static SqlValue null() { return SqlValue(); }

    Type type() const { return _type; }

    int asInt() const { return _intVal; }
    double asDouble() const { return _doubleVal; }
    const std::string& asString() const { return *_strVal; }

private:
    Type _type;
    int _intVal;
    double _doubleVal;
    std::shared_ptr<std::string> _strVal;
};

template<typename T>
T fromString(const std::string& s);

template<>
inline int fromString<int>(const std::string& s) { return std::stoi(s); }

template<>
inline double fromString<double>(const std::string& s) { return std::stod(s); }

template<>
inline std::string fromString<std::string>(const std::string& s) { return s; }

template<>
inline bool fromString<bool>(const std::string& s) { return s == "1" || s == "true"; }

template<typename T>
inline std::string toString(const T& value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

} // namespace toolkit

#define SQL_STRUCT(TYPE, TABLE_NAME, DB_NAME, ...)                                                 \
    static std::string tableName() { return TABLE_NAME; }                                          \
    static std::string dbName() { return DB_NAME; }                                                \
    static std::vector<std::string> getColumnNames() { return {#__VA_ARGS__}; }                    \
    static std::vector<std::pair<std::string, toolkit::SqlValue>> toKeyValuePairs(const TYPE &obj) \
    {                                                                                              \
        std::vector<std::pair<std::string, toolkit::SqlValue>> kv;                                 \
        const char *fields[] = {#__VA_ARGS__};                                                     \
        toolkit::SqlValue values[] = {__VA_ARGS__};                                                \
        size_t count = sizeof(values) / sizeof(toolkit::SqlValue);                                 \
        for (size_t i = 0; i < count; ++i)                                                         \
        {                                                                                          \
            kv.emplace_back(fields[i], values[i]);                                                 \
        }                                                                                          \
        return kv;                                                                                 \
    }                                                                                              \
    static TYPE fromMap(const std::map<std::string, std::string> &m)                               \
    {                                                                                              \
        TYPE obj;                                                                                  \
        applyFromMap(obj, m);                                                                      \
        return obj;                                                                                \
    }                                                                                              \
    static void applyFromMap(TYPE &obj, const std::map<std::string, std::string> &m)

#endif // SQL_SQLMACRO_H