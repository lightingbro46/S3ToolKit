#include <iostream>
#include "Util/logger.h"
#if defined(ENABLE_SQLITE)
#include "Util/SqliteBaseMapper.h"
#include "Util/SqlitePool.h"
#endif
using namespace std;
using namespace toolkit;

struct User {
    std::string id;
    std::string name;
    int age;
    int64_t created_at;
};

#if defined(ENABLE_SQLITE)

#define USER_FIELD_LIST(X) X(id) X(name) X(age) X(created_at)

struct UserMapper {
    SQL_CLASS(User, "users", USER_FIELD_LIST)
};

#endif

int main() {
    // Initialize log
    Logger::Instance().add(std::make_shared<ConsoleChannel> ());

#if defined(ENABLE_SQLITE)
     /*
     * Test method:
     * Please modify the source code according to the actual database situation and then compile and run the test
     */

#if defined(SUPPORT_DYNAMIC_TEMPLATE)
    // Initialize data
    SqlitePool::Ptr pool = std::make_shared<SqlitePool>();
    pool->Init("./test_db.sqlite");
#else
    // Because compiler support for variable parameter templates is required, versions below gcc5.0 generally do not support it, otherwise a compilation error will occur
    ErrorL << "your compiler does not support variable parameter templates!" << endl;
    return -1;
#endif //defined(SUPPORT_DYNAMIC_TEMPLATE)

    // It is recommended to set the database connection pool size to be consistent with the number of CPUs (slightly larger is better)
    pool->setSize(3 + thread::hardware_concurrency());
    SqlitePoolMap::Instance().add("test_db", pool);

    vector<vector<string> > sqlRet;
    // Remove old test table creation and use correct table for User
    SqliteWriter(pool, "CREATE TABLE IF NOT EXISTS users (id TEXT PRIMARY KEY, name TEXT, age INTEGER, created_at INTEGER);") << sqlRet;

    User u1{"123", "Anh", 10, 1234567};

    SqliteHelper helper("test_db");

    SqliteBaseMapper<User, UserMapper> mapper(helper.pool());

    auto ret1 = mapper.insert(u1);
    if (!ret1) {
        DebugL << "Insert data failed";
        return -1;
    } else {
        DebugL << "Insert data success!";
    }

    User u2{"123", "Anh123", 10, 1234567};
    auto ret2 = mapper.updateById(u2);
    if (!ret1) {
        DebugL << "Update data failed";
        return -1;
    } else {
        DebugL << "Update data success!";
    }

    std::unique_ptr<User> u3 = mapper.findById(u2.id);
    if (!u3) {
        DebugL << "Select data failed";
        return -1;
    } else {
        DebugL << "Select data success!";
        if (u3->id == u2.id) {
            DebugL << "Data user correct!";
        }
    }

    auto ret4 = mapper.removeById(u3->id);
    if (!ret4) {
        DebugL << "Delete data failed";
        return -1;
    } else {
        DebugL << "Delete data success!";
    }

    std::vector<User> u4 = mapper.findAll();
    if (!u4.empty()) {
        DebugL << "Select all data failed";
        return -1;
    } else {
        DebugL << "Select all data success!";
        DebugL << "Data empty!";
    }
#else
    ErrorL << "ENABLE_SQLITE not defined!" << endl;
    return -1;
#endif//ENABLE_SQLITE

    return 0;
}
