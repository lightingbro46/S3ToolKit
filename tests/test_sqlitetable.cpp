#include <iostream>
#include "Util/logger.h"
#if defined(ENABLE_SQLITE)
#include "Util/SqliteTable.h"
#endif
using namespace std;
using namespace toolkit;

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

    #define USER_FIELD_LIST(X) X(id) X(name) X(age)
    struct User {
        int id;
        std::string name;
        int age;
        SQL_CLASS(User, "user", "db", USER_FIELD_LIST)
    };
    
#else
    ErrorL << "ENABLE_SQLITE not defined!" << endl;
    return -1;
#endif//ENABLE_SQLITE

    return 0;
}
