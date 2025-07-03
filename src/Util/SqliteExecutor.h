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

//Global Sqlite pool record object, convenient for later management
//Thread-safe    
class SqlitePoolMap : public std::enable_shared_from_this<SqlitePoolMap> {
public:
    using Ptr = std::shared_ptr<SqlitePoolMap>;

    static SqlitePoolMap &Instance();
    ~SqlitePoolMap() = default;

    SqlitePool::Ptr get(const std::string& tag, const std::string &path) {
        std::lock_guard<std::mutex> lck(_mtx);
        auto it = _pools.find(tag);
        if (it != _pools.end()) {
            return it->second;
        }
        return add(tag, path);
    }

private:
    SqlitePoolMap() = default;

    SqlitePool::Ptr add(const std::string& tag, const std::string &path) {
        std::lock_guard<std::mutex> lck(_mtx);
        auto pool = std::make_shared<SqlitePool>();
        auto db_name = tag + ".sqlite";
        auto full_path = path + "/" + db_name;
        pool->Init(full_path);
        pool->setSize(3 + std::thread::hardware_concurrency());
        return _pools.emplace(tag, pool).first->second;
    }

private:
    std::mutex _mtx;
    std::unordered_map<std::string, SqlitePool::Ptr> _pools;
};

class SqliteHelper {
public: 
    using Ptr = std::shared_ptr<SqliteHelper>;

    SqliteHelper(const std::string &tag, const std::string &path) {
        _tag = std::move(tag);
        //Get the pool in the global map for easy management later
        _pool_map = SqlitePoolMap::Instance().shared_from_this();
        _pool =_pool_map->get(tag, path);
    }

    ~SqliteHelper() = default;

    const SqlitePool::Ptr &pool() const {
        return _pool;
    }

private:
    std::string _tag;
    SqlitePool::Ptr _pool;
    SqlitePoolMap::Ptr _pool_map;
};

class SqliteQueryExecutor {
public:
    using Ptr = std::shared_ptr<SqliteQueryExecutor>;

    SqliteQueryExecutor(const std::string &tag, const std::string &path ,EventPoller::Ptr poller = nullptr) {
        _poller = poller ? std::move(poller) : EventPollerPool::Instance().getPoller();
        _helper = std::make_shared<SqliteHelper>(tag, path);
    }

    template <typename... ArgsType>
    bool execDML(ArgsType &&...args) {
        auto pool = _helper->pool();
        return QueryExecutor::execDML<SqlitePool, SqliteWriter>(pool, std::forward<ArgsType>(args)...) > 0;
    }

    template <typename... ArgsType>
    toolkit::SqlitePool::SqlRetType executeRaw(ArgsType &&...args) {
        auto pool = _helper->pool();
        return QueryExecutor::executeRaw<SqlitePool, SqliteWriter>(pool, std::forward<ArgsType>(args)...);
    }

protected:
    SqliteHelper::Ptr _helper;
    toolkit::EventPoller::Ptr _poller;
};

} // namespace toolkit

#endif // SQL_SQLTABLE_H