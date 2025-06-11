#ifndef SQL_SQLTABLE_H
#define SQL_SQLTABLE_H

#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>
#include "Poller/EventPoller.h"
#include "QueryBaseMapper.h"
#include "QueryBuilder.h"
#include "SqlitePool.h"

namespace toolkit {

class SqlitePool;

//Global Sqlite pool record object, convenient for later management
//Thread-safe    
class SqlitePoolMap : public std::enable_shared_from_this<SqlitePoolMap> {
public:
    using Ptr = std::shared_ptr<SqlitePoolMap>;

    static SqlitePoolMap &Instance();
    ~SqlitePoolMap() = default;

    bool add(const std::string& tag, std::shared_ptr<SqlitePool> pool) {
        std::lock_guard<std::mutex> lck(_mtx_pool);
        return _sqlite_pools.emplace(tag, std::move(pool)).second;
    }

    bool del(const std::string &tag) {
        std::lock_guard<std::mutex> lck(_mtx_pool);
        return _sqlite_pools.erase(tag);
    }

    std::shared_ptr<SqlitePool> get(const std::string& tag) {
        std::lock_guard<std::mutex> lck(_mtx_pool);
        auto it = _sqlite_pools.find(tag);
        return it != _sqlite_pools.end() ? it->second : nullptr;
    }

private:
    SqlitePoolMap() = default;

    std::mutex _mtx_pool;
    std::unordered_map<std::string, std::shared_ptr<SqlitePool>> _sqlite_pools;
};

class SqliteHelper {
public: 
    using Ptr = std::shared_ptr<SqliteHelper>;

    SqliteHelper(const std::string &tag) {
        _tag = std::move(tag);
        //Get the pool in the global map for easy management later
        _pool_map = SqlitePoolMap::Instance().shared_from_this();
        _pool =_pool_map->get(tag);
    }

    ~SqliteHelper() = default;

    const std::shared_ptr<SqlitePool> &pool() const {
        return _pool;
    }

private:
    std::string _tag;
    std::shared_ptr<SqlitePool> _pool;
    SqlitePoolMap::Ptr _pool_map;
};

template <typename T, typename Derived>
class SqliteBaseMapper : public BaseMapper<T, Derived> {
public:
    using Ptr = std::shared_ptr<SqliteBaseMapper>;
    using KeyValuePair = std::pair<std::string, variant>;
    using KeyValuePairs = std::vector<KeyValuePair>;

    SqliteBaseMapper(std::shared_ptr<SqlitePool> pool, EventPoller::Ptr poller = nullptr) : _pool(pool) {
        _poller = poller ? std::move(poller) : EventPollerPool::Instance().getPoller();
    }

    bool insert(const T& obj) override {
        auto query = QueryBuilder()
                         .insertInto(Derived::tableName())
                         .values(Derived::toKeyValuePairs(obj));
        auto ret = QueryExecutor::execDML<SqlitePool, SqliteWriter>(_pool, query);
        return ret > 0;
    }

    bool update(const T& obj, const std::string &keyColumn = "id") override {
        const auto& kv = Derived::toKeyValuePairs(obj);
        variant key;
        KeyValuePairs updates;
        for (const auto& pair : kv) {
            if (pair.first == keyColumn) {
                key = pair.second;
            } else {
                updates.push_back(pair);
            }
        }
        auto query = QueryBuilder()
                        .update(Derived::tableName())
                        .set(updates)
                        .where(keyColumn + " = ?", {key});
        auto ret = QueryExecutor::execDML<SqlitePool, SqliteWriter>(_pool, query);
        return ret > 0;
    }

    bool remove(const std::string &id, const std::string &keyColumn = "id") override {
        auto query = QueryBuilder()
                    .deleteFrom(Derived::tableName())
                    .where(keyColumn + " = ?", {id});
        auto ret = QueryExecutor::execDML<SqlitePool, SqliteWriter>(_pool, query);
        return ret > 0;
    }

    std::unique_ptr<T> findById(const std::string &id, const std::string &keyColumn = "id") override {
        auto query = QueryBuilder()
                    .select(Derived::getColumnNames())
                    .from(Derived::tableName())
                    .where(keyColumn + " = ?", {id});
        auto rows = QueryExecutor::executeRaw<SqlitePool, SqliteWriter>(_pool, query);
        if (!rows.empty()) {
            auto ret = Derived::fromVector(rows.front());
            return std::unique_ptr<T>(new T(ret));
        }
        return nullptr;
    }

    std::vector<T> findAll() override {
        auto query = QueryBuilder()
                    .select(Derived::getColumnNames())
                    .from(Derived::tableName());
        auto rows = QueryExecutor::executeRaw<SqlitePool, SqliteWriter>(_pool, query);
        std::vector<T> results;
        for (const auto& row : rows) {
            results.emplace_back(Derived::fromVector(row));
        }
        return results;
    }

protected:
    std::shared_ptr<SqlitePool> _pool;
    EventPoller::Ptr _poller;
};

} // namespace toolkit

#endif // SQL_SQLTABLE_H