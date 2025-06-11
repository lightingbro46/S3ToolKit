#ifndef SQL_SQLMACRO_H
#define SQL_SQLMACRO_H

#include <vector>
#include <string>
#include "mini.h"

namespace toolkit {

// Helper for field assignment from variant (C++11)
template<typename T>
inline void assignField(T& field, const toolkit::variant& v) { field = v.as<T>(); }

template<>
inline void assignField<std::string>(std::string& field, const toolkit::variant& v) { field = v; }

// Helper for assigning all fields from vector
// Usage: fromVectorImpl(obj, vec, obj.field1, obj.field2, ...)
template<typename Obj, typename... Fields>
void fromVectorImpl(Obj& obj, const std::vector<toolkit::variant>& vec, Fields&... fields) {
    size_t i = 0;
    int dummy[] = {0, ((i < vec.size() ? assignField(fields, vec[i++]) : void()), 0)...};
    (void)dummy;
}

// C++11-friendly SQL_CLASS macro: requires FIELD_LIST macro for each struct
// Usage:
//   #define USER_FIELD_LIST(X) X(id) X(name) X(age)
//   struct User { int id; std::string name; int age; SQL_CLASS(User, "user", "db", USER_FIELD_LIST) };
#define SQL_CLASS(CLASS_NAME, TABLE_NAME, FIELD_LIST)                                                   \
    static std::string tableName() { return TABLE_NAME; }                                               \
    static std::vector<std::string> getColumnNames()                                                    \
    {                                                                                                   \
        std::vector<std::string> v;                                                                     \
        FIELD_LIST(SQL_CLASS_FIELD_NAME)                                                                \
        return v;                                                                                       \
    }                                                                                                   \
    static std::vector<toolkit::variant> getColumnValues(const CLASS_NAME &obj)                         \
    {                                                                                                   \
        std::vector<toolkit::variant> v;                                                                \
        FIELD_LIST(SQL_CLASS_FIELD_VALUE)                                                               \
        return v;                                                                                       \
    }                                                                                                   \
    static std::vector<std::pair<std::string, toolkit::variant>> toKeyValuePairs(const CLASS_NAME &obj) \
    {                                                                                                   \
        std::vector<std::pair<std::string, toolkit::variant>> kvs;                                      \
        std::vector<std::string> names = getColumnNames();                                              \
        std::vector<toolkit::variant> values = getColumnValues(obj);                                    \
        for (size_t i = 0; i < names.size() && i < values.size(); ++i)                                  \
        {                                                                                               \
            kvs.push_back(std::make_pair(names[i], values[i]));                                         \
        }                                                                                               \
        return kvs;                                                                                     \
    }                                                                                                   \
    static CLASS_NAME fromVector(const std::vector<std::string> &vec)                                   \
    {                                                                                                   \
        CLASS_NAME obj;                                                                                 \
        size_t i = 0;                                                                                   \
        FIELD_LIST(SQL_CLASS_FIELD_ASSIGN)                                                              \
        return obj;                                                                                     \
    }

#define SQL_CLASS_FIELD_NAME(field) v.push_back(#field);
#define SQL_CLASS_FIELD_VALUE(field) v.push_back(obj.field);
#define SQL_CLASS_FIELD_ASSIGN(field) if (i < vec.size()) assignField(obj.field, vec[i++]);


template <typename T, typename Derived>
class BaseMapper {
public:
    virtual ~BaseMapper() = default;

    virtual bool insert(const T &) = 0;
    virtual bool update(const T &, const std::string&) = 0;
    virtual bool remove(const std::string&, const std::string&) = 0;
    virtual std::unique_ptr<T> findById(const std::string&, const std::string&) = 0;
    virtual std::vector<T> findAll() = 0;
};

} // namespace toolkit

#endif // SQL_SQLMACRO_H