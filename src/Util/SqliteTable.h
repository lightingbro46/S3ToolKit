#ifndef SQL_SQLITETABLE_H
#define SQL_SQLITETABLE_H

#include <unordered_map>
#include "Util/mini.h"
#include "SqlitePool.h"

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

private:
    SqlitePoolMap() = default;

    //Remove pool
    bool del(const std::string &tag);
    //Add pool
    bool add(const std::string &tag, const SqlitePool::Ptr &pool);

private:
    std::mutex _mtx_pool;
    std::unordered_map<std::string, std::weak_ptr<SqlitePool> > _map_pool;
};

class SqliteTable;

class SqliteHelper {
public:
    bool enable = true;

    using Ptr = std::shared_ptr<SqliteHelper>;

    SqliteHelper(const std::weak_ptr<SqliteTable> &table, std::string tag, std::string cls);
    ~SqliteHelper();

    const SqlitePool::Ptr &pool() const;
    const std::string &className() const;

private:
    std::string _cls;
    std::string _tag;
    SqlitePool::Ptr _pool;
    SqlitePoolMap::Ptr _pool_map;
    std::weak_ptr<SqliteTable> _table;
};

struct SqliteRecord;

class SqliteTable : public std::enable_shared_from_this<SqliteTable> {
public:
    using Ptr = std::shared_ptr<SqliteTable>;

    explicit SqliteTable(const std::string &tag, EventPoller::Ptr poller = nullptr);
    virtual ~SqliteTable() = default;

    virtual void insertInto(SqliteRecord record, std::function<void(std::string &err)> &cb) = 0;
    virtual void updateSet(const std::string &guid, SqliteRecord record, std::function<void(std::string &err)> &cb) = 0;
    virtual void selectById(const std::string &guid, std::function<void(std::string &err, SqliteRecord &data)> &cb) = 0;
    virtual void deleteFrom(const std::string &guid, std::function<void(std::string &err)> &cb) = 0;

protected:
    EventPoller::Ptr _poller;
    SqliteHelper::Ptr _helper;
};

} // namespace toolkit

#endif // SQL_SQLITETABLE_H