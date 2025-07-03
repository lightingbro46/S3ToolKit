#ifndef UTIL_QUERYBASEMAPPER_H
#define UTIL_QUERYBASEMAPPER_H

#include <string>
#include <vector>
#include <sstream>

template<typename T>
struct Nullable {
    bool has_value;
    T value;

    Nullable() : has_value(false) {}
    Nullable(const T& v) : has_value(true), value(v) {}

    operator bool() const { return has_value; }
    const T& operator*() const { return value; }
};

template<typename T>
std::string serialize_sql_value(const T& val);

template<>
std::string serialize_sql_value(const int& val) {
    return std::to_string(val);
}

template<>
std::string serialize_sql_value(const std::string& val) {
    return val;
}

template<typename T>
std::string serialize_sql_value(const Nullable<T>& val) {
    if (!val.has_value) return "NULL";
    return serialize_sql_value(val.value);
}

// Vector → JSON-like TEXT
template<typename T>
std::string serialize_sql_value(const std::vector<T>& vec) {
    std::ostringstream oss;
    oss << "'[";
    for (size_t i = 0; i < vec.size(); ++i) {
        oss << serialize_sql_value(vec[i]);
        if (i + 1 < vec.size()) oss << ",";
    }
    oss << "]'";
    return oss.str();
}

template<typename FieldType>
FieldType parse_value(const std::string& s);

template<>
int parse_value<int>(const std::string& s) {
    return std::stoi(s);
}

template<>
std::string parse_value<std::string>(const std::string& s) {
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

template<>
std::vector<int> parse_value<std::vector<int>>(const std::string& s) {
    std::vector<int> result;
    if (s.size() < 2) return result;
    // Remove surrounding quotes
    std::string content = s;
    if (content.front() == '\'' && content.back() == '\'') {
        content = content.substr(1, content.size() - 2);
    }

    // Remove brackets
    if (content.front() == '[' && content.back() == ']') {
        content = content.substr(1, content.size() - 2);
    }

    std::istringstream iss(content);
    std::string token;
    while (std::getline(iss, token, ',')) {
        if (!token.empty()) result.push_back(std::stoi(token));
    }
    return result;
}

template<typename T>
Nullable<T> parse_value(const std::string& s) {
    if (s == "NULL") return Nullable<T>();
    return Nullable<T>(parse_value<T>(s));
}

template<typename T>
Nullable<std::vector<T>> parse_value(const std::string& s) {
    if (s == "NULL") return Nullable<std::vector<T>>();
    return Nullable<std::vector<T>>(parse_value<std::vector<T>>(s));
}

/////////////////////////////////////////////////////////////////////

struct FieldBase {
    virtual std::string get_name() const = 0;
    virtual std::string get_value_sql(const void *obj) const = 0;
    virtual void set_value_from_string(void* obj, const std::string& val) const = 0;
    virtual ~FieldBase() {}
};

template<typename T, typename FieldType>
struct Field : FieldBase {
    std::string name;
    FieldType T::* member;

    Field(const std::string& n, FieldType T::* m) : name(n), member(m) {}

    std::string get_name() const override {
        return name;
    }

    std::string get_value_sql(const void* obj) const override {
        const T* t_obj = static_cast<const T*>(obj);
        const FieldType& val = t_obj->*member;
        return serialize_sql_value(val);
    }

    void set_value_from_string(void* obj, const std::string& val) const override {
        T* t_obj = static_cast<T*>(obj);
        t_obj->*member = parse_value<FieldType>(val);
    }
};

template<typename T>
class BaseMapper {
public:
    BaseMapper(const std::string& n) : _name(n) {}
    ~BaseMapper() {
        for (auto f : fields) delete f;
    }

    std::string getTableName() { return _name; }

    void setTableName(const std::string &name) { _name = name; }

    template<typename FieldType>
    void add_field(const std::string& field_name, FieldType T::* member) {
        fields.push_back(new Field<T, FieldType>(field_name, member));
    }

    std::vector<std::string> getColumnNames() const {
        std::vector<std::string> ret;
        for (size_t i = 0; i < fields.size(); ++i) {
            ret.push_back(fields[i]->get_name());
        }
        return ret;
    }

    std::vector<std::string> getColumnValues(const T &obj) {                                                                                                   \
        std::vector<std::string> ret;                                                                
        for (size_t i = 0; i < fields.size(); ++i) {
            ret.push_back(fields[i]->get_value_sql(&obj));
        }                                                              
        return ret;                                                                                       
    }       

    std::vector<std::pair<std::string, std::string>> toKeyValuePairs(const T &obj) {                                                                                                   \
        std::vector<std::pair<std::string, std::string>> kvs;                                      
        std::vector<std::string> names = getColumnNames();
        std::vector<std::string> values = getColumnValues(obj);
        for (size_t i = 0; i < names.size() && i < values.size(); ++i) {                                                                                               
            kvs.push_back(std::make_pair(names[i], values[i]));                                         
        }                                                                                               
        return kvs;                                                                                     
    }

    std::vector<T> fromVector(const std::vector<std::vector<std::string>>& rows) const {
        std::vector<T> result;
        for (const auto& row : rows) {
            if (row.size() != fields.size()) continue;
            T obj;
            for (size_t i = 0; i < fields.size(); ++i) {
                fields[i]->set_value_from_string(&obj, row[i]);
            }
            result.push_back(obj);
        }
        return result;
    }

private:
    std::string _name;
    std::vector<FieldBase*> fields;
};

#endif // UTIL_QUERYBASEMAPPER_H