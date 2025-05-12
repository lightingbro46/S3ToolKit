#include <iostream>
#include "Util/logger.h"
#if defined(ENABLE_SQLITE)
#include "Util/SqlitePool.h"
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
    SqlitePool::Instance().Init("./test_db.sqlite");
#else
    // Because compiler support for variable parameter templates is required, versions below gcc5.0 generally do not support it, otherwise a compilation error will occur
    ErrorL << "your compiler does not support variable parameter templates!" << endl;
    return -1;
#endif //defined(SUPPORT_DYNAMIC_TEMPLATE)

    // It is recommended to set the database connection pool size to be consistent with the number of CPUs (slightly larger is better)
    SqlitePool::Instance().setSize(3 + thread::hardware_concurrency());

    vector<vector<string> > sqlRet;
    SqliteWriter("create table test_table(user_name  varchar(128),user_id int auto_increment primary key,user_pwd varchar(128));", false) << sqlRet;

    // Synchronous insertion
    SqliteWriter insertSql("insert into test_table(user_name,user_pwd) values('?','?');");
    insertSql<< "s3toolkit" << "123456" << sqlRet;
    // We can know how many pieces of data were inserted, and we can get the rowID of the newly inserted (first) data
    DebugL << "AffectedRows:" << insertSql.getAffectedRows() << ",RowID:" << insertSql.getRowID();

    // Synchronous query
    SqliteWriter sqlSelect("select user_id , user_pwd from test_table where user_name='?' limit 1;") ;
    sqlSelect << "s3toolkit" ;

    vector<vector<string> > sqlRet0;
    vector<list<string> > sqlRet1;
    vector<deque<string> > sqlRet2;
    vector<map<string,string> > sqlRet3;
    vector<unordered_map<string,string> > sqlRet4;
    sqlSelect << sqlRet0;
    sqlSelect << sqlRet1;
    sqlSelect << sqlRet2;
    sqlSelect << sqlRet3;
    sqlSelect << sqlRet4;

    for(auto &line : sqlRet0){
        DebugL << "vector<string> user_id:" << line[0] << ",user_pwd:"<<  line[1];
    }
    for(auto &line : sqlRet1){
        DebugL << "list<string> user_id:" << line.front() << ",user_pwd:"<<  line.back();
    }
    for(auto &line : sqlRet2){
        DebugL << "deque<string> user_id:" << line[0] << ",user_pwd:"<<  line[1];
    }

    for(auto &line : sqlRet3){
        DebugL << "map<string,string> user_id:" << line["user_id"] << ",user_pwd:"<<  line["user_pwd"];
    }

    for(auto &line : sqlRet4){
        DebugL << "unordered_map<string,string> user_id:" << line["user_id"] << ",user_pwd:"<<  line["user_pwd"];
    }

    // Asynchronous deletion
    SqliteWriter insertDel("delete from test_table where user_name='?';");
    insertDel << "s3toolkit" << endl;

    // Note!
    // If the "<<" operator of SqliteWriter is followed by SqlitePool::SqlRetType type, it indicates a synchronous operation and waits for the result
    // If followed by std::endl, it is an asynchronous operation, which completes the sql operation in the background thread.
#else
    ErrorL << "ENABLE_SQLITE not defined!" << endl;
    return -1;
#endif//ENABLE_SQLITE

    return 0;
}
