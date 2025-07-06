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
    SqliteBaseWriter(pool, "create table test_table(user_name TEXT, user_id INTEGER PRIMARY KEY AUTOINCREMENT,user_pwd TEXT);", false) << sqlRet;

    // Synchronous insertion
    SqliteBaseWriter insertSql(pool, "insert into test_table(user_name,user_pwd) values( ?, ? );");
    insertSql<< "s3toolkit" << "123456" << sqlRet;
    // We can know how many pieces of data were inserted, and we can get the rowID of the newly inserted (first) data
    DebugL << "INSERT: AffectedRows:" << insertSql.getAffectedRows() << ",RowID:" << insertSql.getRowID();

    // // Synchronous query
    SqliteBaseWriter sqlSelect(pool, "select user_id , user_pwd from test_table where user_name=? limit 1;") ;
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
    // SqliteBaseWriter removeSql(pool, "delete from test_table where user_name=?;");
    // removeSql << "s3toolkit" << endl;

    {
        SqliteTransaction::Ptr txn = std::make_shared<SqliteTransaction>(pool);
        SqliteTransactionWriter insertSqlWithTransaction(txn, "insert into test_table(user_name,user_pwd) values(?,?);");
        insertSqlWithTransaction << "s3mediakit";
        insertSqlWithTransaction << "123456";
        vector<vector<string>> sqlRet5;
        insertSqlWithTransaction << sqlRet5;
        DebugL << "INSERT TRANSACTION: AffectedRows:" << insertSqlWithTransaction.getAffectedRows() << ",RowID:" << insertSqlWithTransaction.getRowID();

        txn->commit();
    }

    SqliteBaseWriter removeAllSql(pool, "delete from test_table;");
    removeAllSql << endl;

    // Note!
    // If the "<<" operator of SqliteWriter is followed by SqlitePool::SqlRetType type, it indicates a synchronous operation and waits for the result
    // If followed by std::endl, it is an asynchronous operation, which completes the sql operation in the background thread.
#else
    ErrorL << "ENABLE_SQLITE not defined!" << endl;
    return -1;
#endif//ENABLE_SQLITE
    // Waiting for excuting async statement
    sleep(2);

    return 0;
}
