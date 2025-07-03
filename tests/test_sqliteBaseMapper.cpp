#include <iostream>
#include "Util/logger.h"
#if defined(ENABLE_SQLITE)
#include "Util/SqliteBaseMapper.h"
#include "Util/SqlitePool.h"
#endif
using namespace std;
using namespace toolkit;

struct UserEntry {
    std::string id;
    std::string name;
    int age;
    int64_t created_at;
    Nullable<int> update_at;
};

static const string user_table = "user";
static const string user_db = "test_db";

#if defined(ENABLE_SQLITE)

class UserEntity : public UserEntry, BaseMapper<UserEntry>, SqliteQueryExecutor {
public:
    UserEntity(): UserEntry(), BaseMapper<UserEntry>(user_table), SqliteQueryExecutor(user_db) {}
    
    void save() {}

    static void updateById() {}

    static void removeById() {}

    static std::unique_ptr<UserEntry> findById() { return nullptr; }
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

    vector<vector<string> > sqlRet;
    // Remove old test table creation and use correct table for User
    SqliteWriter(pool, "CREATE TABLE IF NOT EXISTS users (id TEXT PRIMARY KEY, name TEXT, age INTEGER, created_at INTEGER);") << sqlRet;

    UserEntity u1;
    u1.id = "123";
    u1.name = "123";
    u1.age = 10;
    u1.created_at = 1234567;
    u1.save();
    DebugL << "Insert data success!";

    u1.update_at = 11111;
    u1.save();
    DebugL << "Update data success!";

    std::unique_ptr<UserEntry> u3 = UserEntity::findById();
    DebugL << "Select data success!";

    UserEntity::removeById();
    DebugL << "Delete data success!";

#else
    ErrorL << "ENABLE_SQLITE not defined!" << endl;
    return -1;
#endif//ENABLE_SQLITE

    return 0;
}
