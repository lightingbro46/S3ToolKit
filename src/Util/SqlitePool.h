#ifndef SQL_SQLITEPOOL_H_
#define SQL_SQLITEPOOL_H_

#include "Poller/Timer.h"
#include "ResourcePool.h"
#include "SqliteConnection.h"
#include "Thread/WorkThreadPool.h"
#include "logger.h"

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>

namespace toolkit {

class SqlitePool : public std::enable_shared_from_this<SqlitePool> {
public:
    using Ptr        = std::shared_ptr<SqlitePool>;
    using PoolType   = ResourcePool<SqliteConnection>;
    using SqlRetType = std::vector<std::vector<std::string>>;

    static SqlitePool &Instance();

    SqlitePool() {
        _threadPool = WorkThreadPool::Instance().getExecutor();
        _timer      = std::make_shared<Timer>(
            30,
            [this]() {
                flushError();
                return true;
            },
            nullptr);
    }

    ~SqlitePool() {
        _timer.reset();
        flushError();
        _threadPool.reset();
        _pool.reset();
        InfoL;
    }

    /**
     * Sets the number of loop pool objects
     * @param size
     */
    void setSize(int size) {
        checkInited();
        _pool->setSize(size);
    }

    /**
     * Initializes the loop pool and sets the database connection parameters
     * @tparam Args
     * @param arg
     */
    template <typename... Args> void Init(Args&&... arg) {
        _pool.reset(new PoolType(std::forward<Args>(arg)...));
        _pool->obtain();
    }

    /**
     * Asynchronously executes SQL
     * @param str SQL statement
     * @param tryCnt Number of retries
     */
    template <typename... Args> void asyncQuery(Args&&... args) {
        asyncQuery_l(SqliteConnection::queryString(std::forward<Args>(args)...));
    }

    /**
     * Synchronously executes SQL
     * @tparam Args Variable parameter type list
     * @param arg Variable parameter list
     * @return Number of affected rows
     */

    template <typename... Args> int64_t syncQuery(Args&&... arg) {
        checkInited();
        typename PoolType::ValuePtr sqlite;
        try {
            // Capture execution exceptions
            sqlite = _pool->obtain();
            return sqlite->query(std::forward<Args>(arg)...);
        } catch (std::exception& e) {
            sqlite.quit();
            throw;
        }
    }

    /**
     * Escapes SQL
     * @param str
     * @return
     */
    std::string escape(const std::string& str) {
        checkInited();
        return _pool->obtain()->escape(const_cast<std::string&>(str));
    }

private:
    /**
     * Asynchronously executes SQL
     * @param sql SQL statement
     * @param tryCnt Number of retries
     */
    void asyncQuery_l(const std::string& sql, int tryCnt = 3) {
        auto lam = [this, sql, tryCnt]() {
            int64_t rowID;
            auto    cnt = tryCnt - 1;
            try {
                syncQuery(rowID, sql);
            } catch (std::exception& ex) {
                if (cnt > 0) {
                    // Retry on failure
                    std::lock_guard<std::mutex> lk(_error_query_mutex);
                    sqlQuery                    query(sql, cnt);
                    _error_query.push_back(query);
                } else {
                    WarnL << "SqlitePool::syncQuery failed: " << ex.what();
                }
            }
        };
        _threadPool->async(lam);
    }

    /**
     * Periodically retries failed SQL
     */
    void flushError() {
        decltype(_error_query) query_copy;
        {
            std::lock_guard<std::mutex> lck(_error_query_mutex);
            query_copy.swap(_error_query);
        }
        for (auto& query : query_copy) {
            asyncQuery(query.sql_str, query.tryCnt);
        }
    }

    /**
     * Checks if the database connection pool is initialized
     */
    void checkInited() {
        if (!_pool) {
            throw SqliteException("SqlitePool::checkInited", "Sqlite connection pool not initialized");
        }
    }

private:
    struct sqlQuery {
        sqlQuery(const std::string& sql, int cnt) : sql_str(sql), tryCnt(cnt) {}

        std::string sql_str;
        int         tryCnt = 0;
    };

private:
    std::deque<sqlQuery>      _error_query;
    TaskExecutor::Ptr         _threadPool;
    std::mutex                _error_query_mutex;
    std::shared_ptr<PoolType> _pool;
    Timer::Ptr                _timer;
};

/**
 * SQL statement generator, generates SQL statements through the '?' placeholder
 */
class SqliteStream {
public:
    SqliteStream(const SqlitePool::Ptr pool, const char *sql) : _pool(pool), _sql(sql) {}

    ~SqliteStream() {
        _pool.reset();
    }

    template <typename T> SqliteStream& operator<<(T&& data) {
        auto pos = _sql.find('?', _startPos);
        if (pos == std::string::npos) {
            return *this;
        }
        _str_tmp.str("");
        _str_tmp << std::forward<T>(data);
        std::string str = _pool->escape(_str_tmp.str());
        _startPos       = pos + str.size();
        _sql.replace(pos, 1, str);
        return *this;
    }

    const std::string& operator<<(std::ostream& (*f)(std::ostream&)) const { return _sql; }

    operator std::string() { return _sql; }

private:
    SqlitePool::Ptr        _pool;
    std::stringstream      _str_tmp;
    std::string            _sql;
    std::string::size_type _startPos = 0;
};

/**
 * SQL query executor
 */
class SqliteWriter {
public:
    /**
     * Constructor
     * @param sql SQL template with '?' placeholder
     * @param throwAble Whether to throw exceptions
     */
    SqliteWriter(const SqlitePool::Ptr pool, const char *sql, bool throwAble = true) : _pool(pool), _sqliteStream(pool, sql), _throwAble(throwAble) {}

    ~SqliteWriter() {
        _pool.reset();
    }

    /**
     * Replaces '?' placeholders with input parameters to generate SQL statements; may throw
     exceptions
     * @tparam T Parameter type
     * @param data Parameter
     * @return Self-reference
     */
    template <typename T> SqliteWriter& operator<<(T&& data) {
        try {
            _sqliteStream << std::forward<T>(data);
        } catch (std::exception& ex) {
            // May throw exceptions when escaping SQL
            if (!_throwAble) {
                WarnL << "Commit sql failed: " << ex.what();
            } else {
                throw;
            }
        }
        return *this;
    }

    /**
     * Asynchronously executes SQL, does not throw exceptions
     * @param f std::endl
     */
    void operator<<(std::ostream& (*f)(std::ostream&)) {
        // Asynchronously executes SQL, does not throw exceptions
        _pool->asyncQuery((std::string)_sqliteStream);
    }

    /**
     * Synchronously executes SQL, may throw exceptions
     * @tparam Row Data row type, can be vector<string>/list<string> or other types that support
     obj.emplace_back("value") operations
     *              Can also be map<string,string>/Json::Value or other types that support
     obj["key"] = "value" operations
     * @param ret Data storage object
     * @return Number of affected rows
     */
    template <typename Row> int64_t operator<<(std::vector<Row>& ret) {
        try {
            _affectedRows = _pool->syncQuery(_rowId, ret, (std::string)_sqliteStream);
        } catch (std::exception& ex) {
            if (!_throwAble) {
                WarnL << "SqlitePool::syncQuery failed: " << ex.what();
            } else {
                throw;
            }
        }
        return _affectedRows;
    }

    /**
     * Returns the rowid inserted into the database when inserting data
     * @return
     */
    int64_t getRowID() const { return _rowId; }

    /**
     * Returns the number of rows affected in the database
     * @return
     */
    int64_t getAffectedRows() const { return _affectedRows; }

private:
    SqlitePool::Ptr _pool;
    SqliteStream _sqliteStream;
    int64_t   _rowId        = -1;
    int64_t   _affectedRows = -1;
    bool      _throwAble    = true;
};

} /* namespace toolkit */
#endif /* SQL_SQLITEPOOL_H_ */
