#ifndef SQL_SQLITETABLE_H
#define SQL_SQLITETABLE_H

#include <unordered_map>
#include "Util/mini.h"
#include "SqlitePool.h"
#include "SqlQueryBuilder.h"

namespace toolkit {

//Global Sqlite pool record object, convenient for later management
//Thread-safe
class SqlitePoolMap : public std::enable_shared_from_this<SqlitePoolMap> {
public:
    using Ptr = std::shared_ptr<SqlitePoolMap>;

    //Singleton
    static SqlitePoolMap &Instance();
    ~SqlitePoolMap() = default;

    //Get pool
    SqlitePool::Ptr get(const std::string &tag);
    void for_each_pool(const std::function<void(const std::string &id, const SqlitePool::Ptr &pool)> &cb);

    //Add pool
    bool add(const std::string &tag, const SqlitePool::Ptr &pool);
    //Remove pool
    bool del(const std::string &tag);
private:
    SqlitePoolMap() = default;

private:
    std::mutex _mtx_pool;
    std::unordered_map<std::string, std::weak_ptr<SqlitePool> > _map_pool;
};

class SqliteHelper {
public:
    bool enable = true;

    using Ptr = std::shared_ptr<SqliteHelper>;

    SqliteHelper(std::string tag, std::string cls);
    ~SqliteHelper();

    const SqlitePool::Ptr &pool() const;
    const std::string &className() const;

private:
    std::string _cls;
    std::string _tag;
    SqlitePool::Ptr _pool;
    SqlitePoolMap::Ptr _pool_map;
};

template<typename T>
class SqliteTable {
public:
    SqliteTable() = default;
    explicit SqliteTable(const T& obj) : _data(obj), _helper(dbName()) {}

    virtual ~SqliteTable() = default;

    virtual const std::string& tableName() const {
        return T::tableName();
    }

    virtual const std::string& dbName() const {
        return T::dbName();
    }

    virtual std::vector<std::string> getColumnNames() const {
        return T::getColumnNames();
    }

    virtual std::vector<std::pair<std::string, SqlValue>>  toKeyValuePairs() const {
        return T::toKeyValuePairs(_data);
    }

    QueryBuilder toInsertQuery() const {
        return QueryBuilder().insertInto(tableName()).values(toKeyValuePairs());
    }

    QueryBuilder toUpdateQuery(const std::string& keyColumn = "id") const {
        const auto& kv = toKeyValuePairs();
        SqlValue key;
        std::vector<std::pair<std::string, SqlValue>> updates;
        for (const auto& pair : kv) {
            if (pair.first == keyColumn) {
                key = pair.second;
            } else {
                updates.push_back(pair);
            }
        }
        return QueryBuilder().update(tableName()).set(updates).where(keyColumn + " = ?", {key});
    }

    QueryBuilder toDeleteQuery(const std::string& keyColumn = "id") const {
        const auto& kv = toKeyValuePairs();
        for (const auto& pair : kv) {
            if (pair.first == keyColumn) {
                return QueryBuilder().deleteFrom(tableName()).where(keyColumn + " = ?", {pair.second});
            }
        }
        throw std::runtime_error("Primary key '" + keyColumn + "' not found in struct");
    }

    QueryBuilder toSelectQuery(const std::string& keyColumn = "id") const {
        const auto& kv = toKeyValuePairs();
        for (const auto& pair : kv) {
            if (pair.first == keyColumn) {
                return QueryBuilder().select(getColumnNames()).from(tableName()).where(keyColumn + " = ?", {pair.second});
            }
        }
        throw std::runtime_error("Primary key '" + keyColumn + "' not found in struct");
    }

    const T& data() const { return _data; }
    void setData(const T& data) { _data = data; }

protected:
    T _data;
    SqliteHelper::Ptr _helper;
};

} // namespace toolkit

#endif // SQL_SQLITETABLE_H