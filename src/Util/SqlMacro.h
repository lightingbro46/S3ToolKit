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
    enum Type {
        TYPE_NULL,
        TYPE_INT,
        TYPE_UINT64,
        TYPE_DOUBLE,
        TYPE_STRING
    };

    SqlValue() : _type(TYPE_NULL), _intVal(0), _uint64Val(0), _doubleVal(0.0) {}
    SqlValue(int v) : _type(TYPE_INT), _intVal(v), _uint64Val(0), _doubleVal(0.0) {}
    SqlValue(uint64_t v) : _type(TYPE_UINT64), _intVal(0), _uint64Val(v), _doubleVal(0.0) {}
    SqlValue(double v) : _type(TYPE_DOUBLE), _intVal(0), _uint64Val(0), _doubleVal(v) {}
    SqlValue(const std::string& v) : _type(TYPE_STRING), _strVal(v), _intVal(0), _uint64Val(0), _doubleVal(0.0) {}
    SqlValue(const char* v) : _type(TYPE_STRING), _strVal(v), _intVal(0), _uint64Val(0), _doubleVal(0.0) {}

    Type type() const { return _type; }

    int asInt() const {
        if (_type == TYPE_INT) return _intVal;
        if (_type == TYPE_UINT64) return static_cast<int>(_uint64Val);
        if (_type == TYPE_DOUBLE) return static_cast<int>(_doubleVal);
        if (_type == TYPE_STRING) return std::stoi(_strVal);
        return 0;
    }
    uint64_t asUint64() const {
        if (_type == TYPE_UINT64) return _uint64Val;
        if (_type == TYPE_INT) return static_cast<uint64_t>(_intVal);
        if (_type == TYPE_DOUBLE) return static_cast<uint64_t>(_doubleVal);
        if (_type == TYPE_STRING) return std::stoull(_strVal);
        return 0;
    }
    double asDouble() const {
        if (_type == TYPE_DOUBLE) return _doubleVal;
        if (_type == TYPE_INT) return _intVal;
        if (_type == TYPE_UINT64) return static_cast<double>(_uint64Val);
        if (_type == TYPE_STRING) return std::stod(_strVal);
        return 0.0;
    }
    std::string asString() const {
        if (_type == TYPE_STRING) return _strVal;
        if (_type == TYPE_INT) return std::to_string(_intVal);
        if (_type == TYPE_UINT64) return std::to_string(_uint64Val);
        if (_type == TYPE_DOUBLE) return std::to_string(_doubleVal);
        return "NULL";
    }
    bool isNull() const { return _type == TYPE_NULL; }

    bool operator==(const SqlValue& other) const {
        if (_type != other._type) return false;
        switch (_type) {
            case TYPE_INT: return _intVal == other._intVal;
            case TYPE_UINT64: return _uint64Val == other._uint64Val;
            case TYPE_DOUBLE: return _doubleVal == other._doubleVal;
            case TYPE_STRING: return _strVal == other._strVal;
            case TYPE_NULL: default: return true;
        }
    }

private:
    Type _type;
    int _intVal;
    uint64_t _uint64Val;
    double _doubleVal;
    std::string _strVal;
};

// Helper for field assignment from SqlValue (C++11)
template<typename T>
inline void assignField(T& field, const toolkit::SqlValue& v);

template<>
inline void assignField<int>(int& field, const toolkit::SqlValue& v) { field = v.asInt(); }

template<>
inline void assignField<double>(double& field, const toolkit::SqlValue& v) { field = v.asDouble(); }

template<>
inline void assignField<std::string>(std::string& field, const toolkit::SqlValue& v) { field = v.asString(); }

template<>
inline void assignField<uint64_t>(uint64_t& field, const toolkit::SqlValue& v) { field = v.asUint64(); }

// Helper for assigning all fields from vector
// Usage: fromVectorImpl(obj, vec, obj.field1, obj.field2, ...)
template<typename Obj, typename... Fields>
void fromVectorImpl(Obj& obj, const std::vector<toolkit::SqlValue>& vec, Fields&... fields) {
    size_t i = 0;
    int dummy[] = {0, ((i < vec.size() ? assignField(fields, vec[i++]) : void()), 0)...};
    (void)dummy;
}

// C++11-friendly SQL_CLASS macro: requires FIELD_LIST macro for each struct
// Usage:
//   #define USER_FIELD_LIST(X) X(id) X(name) X(age)
//   struct User { int id; std::string name; int age; SQL_CLASS(User, "user", "db", USER_FIELD_LIST) };
#define SQL_CLASS(CLASS_NAME, TABLE_NAME, DB_NAME, FIELD_LIST)                     \
    static std::string dbName() { return DB_NAME; }                                \
    static std::string tableName() { return TABLE_NAME; }                          \
    static std::vector<std::string> getColumnNames()                               \
    {                                                                              \
        std::vector<std::string> v;                                                \
        FIELD_LIST(SQL_CLASS_FIELD_NAME)                                           \
        return v;                                                                  \
    }                                                                              \
    std::vector<toolkit::SqlValue> getColumnValues() const                         \
    {                                                                              \
        std::vector<toolkit::SqlValue> v;                                          \
        FIELD_LIST(SQL_CLASS_FIELD_VALUE)                                          \
        return v;                                                                  \
    }                                                                              \
    std::vector<std::pair<std::string, toolkit::SqlValue>> toKeyValuePairs() const \
    {                                                                              \
        std::vector<std::pair<std::string, toolkit::SqlValue>> kvs;                \
        std::vector<std::string> names = getColumnNames();                         \
        std::vector<toolkit::SqlValue> values = getColumnValues();                 \
        for (size_t i = 0; i < names.size() && i < values.size(); ++i)             \
        {                                                                          \
            kvs.push_back(std::make_pair(names[i], values[i]));                    \
        }                                                                          \
        return kvs;                                                                \
    }                                                                              \
    static CLASS_NAME fromVector(const std::vector<toolkit::SqlValue> &vec)        \
    {                                                                              \
        CLASS_NAME obj;                                                            \
        size_t i = 0;                                                              \
        FIELD_LIST(SQL_CLASS_FIELD_ASSIGN)                                         \
        return obj;                                                                \
    }

#define SQL_CLASS_FIELD_NAME(field) v.push_back(#field);
#define SQL_CLASS_FIELD_VALUE(field) v.push_back(this->field);
#define SQL_CLASS_FIELD_VALUE_UINT64(field) v.push_back(static_cast<uint64_t>(this->field));
#define SQL_CLASS_FIELD_ASSIGN(field) if (i < vec.size()) assignField(obj.field, vec[i++]);

} // namespace toolkit

#endif // SQL_SQLMACRO_H