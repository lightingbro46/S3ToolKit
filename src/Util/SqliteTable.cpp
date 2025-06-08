#if defined(ENABLE_SQLITE)

#include "SqliteTable.h"
#include "onceToken.h"
#include "util.h"

#include <memory>

using namespace std;

namespace toolkit {

INSTANCE_IMP(SqlitePoolMap)

bool SqlitePoolMap::add(const string &tag, const SqlitePool::Ptr &pool) {
    lock_guard<mutex> lck(_mtx_pool);
    return _map_pool.emplace(tag, std::move(pool)).second;
}

bool SqlitePoolMap::del(const string &tag) {
    lock_guard<mutex> lck(_mtx_pool);
    return _map_pool.erase(tag);
}

SqlitePool::Ptr SqlitePoolMap::get(const string &tag) {
    lock_guard<mutex> lck(_mtx_pool);
    auto it = _map_pool.find(tag);
    if (it == _map_pool.end()) {
        return nullptr;
    }
    return it->second.lock();
}

void SqlitePoolMap::for_each_pool(const function<void(const string &id, const SqlitePool::Ptr &pool)> &cb) {
    lock_guard<mutex> lck(_mtx_pool);
    for (auto it = _map_pool.begin(); it != _map_pool.end();) {
        auto session = it->second.lock();
        if (!session) {
            it = _map_pool.erase(it);
            continue;
        }
        cb(it->first, session);
        ++it;
    }
}

////////////////////////////////////////////////////////////////////////////////////

SqliteHelper::SqliteHelper(string tag, string cls) {
    _tag = std::move(tag);
    _cls = std::move(cls);
    //Get the pool in the global map for easy management later
    _pool_map = SqlitePoolMap::Instance().shared_from_this();
    _pool =_pool_map->get(tag);
}

SqliteHelper::~SqliteHelper() {
    _pool.reset();
    _pool_map.reset();
}

const SqlitePool::Ptr &SqliteHelper::pool() const {
    return _pool;
}

const std::string &SqliteHelper::className() const {
    return _cls;
}

} /* namespace toolkit */

#endif // defined(ENABLE_SQLITE)
