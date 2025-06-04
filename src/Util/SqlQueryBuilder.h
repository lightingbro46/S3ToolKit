#ifndef UTIL_SQLQUERYBUILDER_H_
#define UTIL_SQLQUERYBUILDER_H_

#include <string>
#include <vector>
#include <sstream>
#include <utility>
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

class QueryBuilder {
public:
    enum class Type { SELECT, INSERT, UPDATE, DELETE };

    explicit QueryBuilder(Type type);

    // SELECT
    QueryBuilder& select(const std::vector<std::string>& columns);
    QueryBuilder& from(const std::string& table);

    // UPDATE
    QueryBuilder& update(const std::string& table);
    QueryBuilder& set(const std::string& column, const SqlValue& value);

    // INSERT
    QueryBuilder& insertInto(const std::string& table);
    QueryBuilder& values(const std::vector<std::string>& columns, const std::vector<SqlValue>& values);

    // DELETE
    QueryBuilder& deleteFrom(const std::string& table);

    // Common clauses
    QueryBuilder& where(const std::string& condition, const std::vector<SqlValue>& params = {});
    QueryBuilder& join(const std::string& clause);
    QueryBuilder& leftJoin(const std::string& clause);
    QueryBuilder& rightJoin(const std::string& clause);
    QueryBuilder& groupBy(const std::string& clause);
    QueryBuilder& having(const std::string& clause);
    QueryBuilder& orderBy(const std::string& clause);
    QueryBuilder& limit(int limit);
    QueryBuilder& offset(int offset);

    std::string build() const;
    const std::vector<SqlValue>& getParams() const;

private:
    Type _type;
    std::vector<std::string> _selectColumns;
    std::string _table;
    std::vector<std::string> _joinClauses;
    std::string _whereClause;
    std::vector<SqlValue> _whereParams;
    std::string _groupByClause;
    std::string _havingClause;
    std::string _orderByClause;
    int _limit;
    int _offset;

    // For UPDATE
    std::vector<std::pair<std::string, SqlValue>> _updateSet;

    // For INSERT
    std::vector<std::string> _insertColumns;
    std::vector<SqlValue> _insertValues;
};

#define SQL_CLASS(CLASS_NAME, TABLE_NAME, ...)                                 \
    static std::string staticTableName() { return TABLE_NAME; }                \
    std::string tableName() const { return TABLE_NAME; }                       \
    static std::vector<std::string> getColumnNames() {                         \
        return {__VA_ARGS__};                                                  \
    }                                                                          \
    std::vector<SqlValue> getColumnValues() const {                            \
        return {__VA_ARGS__};                                                  \
    }

template<typename T>
QueryBuilder buildInsertQuery(const T& obj) {
    auto columns = T::getColumnNames();
    auto values = obj.getColumnValues();
    return QueryBuilder(QueryBuilder::Type::INSERT)
        .insertInto(obj.tableName())
        .values(columns, values);
}

template<typename T>
QueryBuilder buildUpdateQuery(const T& obj, const std::string& whereCond, const std::vector<SqlValue>& whereParams) {
    QueryBuilder qb(QueryBuilder::Type::UPDATE);
    qb.update(obj.tableName());

    auto columns = T::getColumnNames();
    auto values = obj.getColumnValues();
    for (size_t i = 0; i < columns.size(); ++i) {
        qb.set(columns[i], values[i]);
    }

    return qb.where(whereCond, whereParams);
}

template<typename T>
QueryBuilder buildSelectQuery(const std::string& whereCond = "", const std::vector<SqlValue>& params = {}) {
    return QueryBuilder(QueryBuilder::Type::SELECT)
        .select(T::getColumnNames())
        .from(T::staticTableName())
        .where(whereCond, params);
}

template<typename T>
QueryBuilder buildDeleteQuery(const std::string& whereCond = "", const std::vector<SqlValue>& params = {}) {
    return QueryBuilder(QueryBuilder::Type::DELETE)
        .deleteFrom(T::staticTableName())
        .where(whereCond, params);
}

} // namespace toolkit 

#endif // UTIL_SQLQUERYBUILDER_H_