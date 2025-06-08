#ifndef UTIL_SQLQUERYBUILDER_H_
#define UTIL_SQLQUERYBUILDER_H_

#include <utility>
#include <functional>
#include "SqlMacro.h"

namespace toolkit {

class QueryBuilder {
public:
    enum class Type { SELECT, INSERT, UPDATE, DELETE };

    QueryBuilder();

    // SELECT
    QueryBuilder& select(const std::vector<std::string>& columns);
    QueryBuilder& from(const std::string& table);

    // UPDATE
    QueryBuilder& update(const std::string& table);
    QueryBuilder& set(const std::vector<std::pair<std::string, SqlValue>>& keyValues);

    // INSERT
    QueryBuilder& insertInto(const std::string& table);
    QueryBuilder& values(const std::vector<std::pair<std::string, SqlValue>>& keyValues);

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

class QueryExecutor {
public:
    // Execute query and return raw rows
    template<typename Pool, typename Writter>
    static std::vector<std::map<std::string, std::string>> executeRaw(const std::shared_ptr<Pool> &pool, const QueryBuilder& builder) {
        Writter writer(pool,  builder.build());
        auto params = builder.getParams();
        if (!params.empty()) {
            for (size_t i = 0; i < params.size(); ++i) {
                writer << params[i];
            }
        }
        std::vector<std::map<std::string, std::string>> rows;
        writer << rows;
        return rows;
    }

    // Execute query and convert rows to vector<T> using fromMap(...)
    template<typename Pool, typename Writter, typename T>
    static std::vector<T> execute(const std::shared_ptr<Pool> &pool, const QueryBuilder& builder, std::function<T(const std::map<std::string, std::string>)> fromMap) {
        Writter writer(pool, builder.build());
        writer << builder;
        auto params = builder.getParams();
        if (!params.empty()) {
            for (size_t i = 0; i < params.size(); ++i) {
                writer << params[i];
            }
        }
        std::vector<std::map<std::string, std::string>> rows;
        writer << rows;

        std::vector<T> results;
        for (const auto& row : rows) {
            results.emplace_back(fromMap(row));
        }
        return results;
    }

    // Execute INSERT/UPDATE/DELETE and return affected row count
    template<typename Pool, typename Writter>
    static int execDML(const std::shared_ptr<Pool> &pool, const QueryBuilder& builder) {
        Writter writer(pool, builder.build());
        auto params = builder.getParams();
        if (!params.empty()) {
            for (size_t i = 0; i < params.size(); ++i) {
                writer << params[i];
            }
        }
        std::vector<std::vector<std::string>> ret;
        writer << ret;
        return writer.getAffectedRows();
    }
};

} // namespace toolkit

#endif // UTIL_SQLQUERYBUILDER_H_