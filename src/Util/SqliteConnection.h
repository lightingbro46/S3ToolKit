#ifndef SQL_SQLITECONNECTION_H
#define SQL_SQLITECONNECTION_H

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <list>
#include <deque>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include "logger.h"
#include "util.h"
#include <sqlite3.h>

#if defined(_WIN32)
#pragma comment(lib, "sqlite3")
#endif

namespace toolkit {

/**
 * Database exception class
 */
class SqliteException : public std::exception {
public:
    SqliteException(const std::string &sql, const std::string &err) {
        _sql = sql;
        _err = err;
    }

    virtual const char *what() const noexcept {
        return _err.data();
    }

    const std::string &getSql() const {
        return _sql;
    }

private:
    std::string _sql;
    std::string _err;
};

/**
 * SqliteDeleter - RAII helper to close connection
 */
struct SqliteDeleter {
    void operator() (sqlite3 * db) const {
        if (db) sqlite3_close(db);
    }
};
using SqliteDB = std::unique_ptr<sqlite3, SqliteDeleter>;

/**
 * StmtDeleter -  RAII helper to finalize stmt
 */
struct StmtDeleter {
    void operator()(sqlite3_stmt* stmt) const {
        if (stmt) sqlite3_finalize(stmt);
    }
};
using SqliteStmt = std::unique_ptr<sqlite3_stmt, StmtDeleter>;

struct SqliteFreeDeleter {
    void operator()(char* p) const noexcept {
        if (p) sqlite3_free(p);
    }
};
using SqliteStringPtr = std::unique_ptr<char, SqliteFreeDeleter>;

/**
 * Sqlite connection
 */
class SqliteConnection {
public:
    /**
     * Constructor
     * @param dbname Database name
     */
    explicit SqliteConnection(const std::string &dbname) {
        sqlite3* db = nullptr;
        if (sqlite3_open(dbname.c_str(), &db) != SQLITE_OK) {
            std::string errorMsg = sqlite3_errmsg(db);
            sqlite3_close(db);
            throw SqliteException("sqlite3_open", errorMsg);
        }
        _db = SqliteDB(db);

        // Thiết lập chế độ hỗ trợ UTF-8
        sqlite3_exec(_db.get(), "PRAGMA encoding = \"UTF-8\";", nullptr, nullptr, nullptr);
    }

    ~SqliteConnection() { _db.reset(); }

    /**
     * Execute SQL in sqlite3 style, no data returned
     * BEGIN TRANSACTION/COMMIT/ROLLBACK
     * @param rowId Insert rowid when inserting
     * @param fmt printf type fmt
     * @param arg Variable argument list
     * @return Affected rows
     */
    template <typename Fmt, typename... Args>
    int64_t query(Fmt &&fmt, Args &&...arg) {
        auto stmt = queryString(std::forward<Fmt>(fmt), std::forward<Args>(arg)...);
        auto tmp = expandedSQL(stmt.get());
        DebugL << "Expanded sql: " << tmp;
        if (doQuery(tmp) != SQLITE_DONE) {
            throw SqliteException(tmp, sqlite3_errmsg(_db.get()));
        }
        return SQLITE_OK;
    }

    /**
     * Execute SQL in sqlite3 style, no data returned
     * INSERT/UPDATE/DELETE
     * @param rowId Insert rowid when inserting
     * @param fmt printf type fmt
     * @param arg Variable argument list
     * @return Affected rows
     */
    template <typename Fmt, typename... Args>
    int64_t query(int64_t &rowId, Fmt &&fmt, Args &&...arg) {
        auto stmt = queryString(std::forward<Fmt>(fmt), std::forward<Args>(arg)...);
        auto tmp = expandedSQL(stmt.get());
        DebugL << "Expanded sql: " << tmp;
        if (doQuery(tmp) != SQLITE_DONE) {
            throw SqliteException(tmp, sqlite3_errmsg(_db.get()));
        }
        rowId = sqlite3_last_insert_rowid(_db.get());
        return sqlite3_changes(_db.get());
    }

    /**
     * Execute SQL in printf style, and return list type result (excluding column names)
     * @param rowId Insert rowid when inserting
     * @param ret Returned data list
     * @param fmt printf type fmt
     * @param arg Variable argument list
     * @return Affected rows
     */
    template <typename Fmt, typename... Args>
    int64_t query(int64_t &rowId, std::vector<std::vector<std::string>> &ret, Fmt &&fmt, Args &&...arg) {
        return queryList(rowId, ret, std::forward<Fmt>(fmt), std::forward<Args>(arg)...);
    }

    template <typename Fmt, typename... Args>
    int64_t query(int64_t &rowId, std::vector<std::list<std::string>> &ret, Fmt &&fmt, Args &&...arg) {
        return queryList(rowId, ret, std::forward<Fmt>(fmt), std::forward<Args>(arg)...);
    }

    template <typename Fmt, typename... Args>
    int64_t query(int64_t &rowId, std::vector<std::deque<std::string>> &ret, Fmt &&fmt, Args &&...arg) {
        return queryList(rowId, ret, std::forward<Fmt>(fmt), std::forward<Args>(arg)...);
    }
    
    /**
     * Execute SQL in printf style, and return Map type result (including column names)
     * @param rowId Insert rowid when inserting
     * @param ret Returned data list
     * @param fmt printf type fmt
     * @param arg Variable argument list
     * @return Affected rows
     */
    template <typename Map, typename Fmt, typename... Args>
    int64_t query(int64_t &rowId, std::vector<Map> &ret, Fmt &&fmt, Args &&...arg) {
        auto stmt = queryString(std::forward<Fmt>(fmt), std::forward<Args>(arg)...);
        DebugL << "Expanded sql: " << expandedSQL(stmt.get());

        ret.clear();
        int columnCount = sqlite3_column_count(stmt.get());

        int rc = 0;
        while ((rc = doQuery(stmt.get())) == SQLITE_ROW) {
            // SELECT
            ret.emplace_back();
            auto& back = ret.back();
            for (int i = 0; i < columnCount; ++i) {
                const char* name = sqlite3_column_name(stmt.get(), i);
                const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), i));
                back[name ? name : ""] = value ? value : "";
            }
        }
        if (rc != SQLITE_DONE) {
            throw SqliteException(expandedSQL(stmt.get()), sqlite3_errmsg(_db.get()));
        } 

        // INSERT/UPDATE/DELETE returns positive number
        // SELECT return 0;
        rowId = sqlite3_last_insert_rowid(_db.get());
        return sqlite3_changes(_db.get());
        
    }

    template <typename... Args>
    SqliteStmt queryString(const char *fmt, Args &&...arg) {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(_db.get(), fmt, -1, &stmt, nullptr) != SQLITE_OK) {
            throw SqliteException(sqlite3_sql(stmt), sqlite3_errmsg(_db.get()));
        }

        bindAll(stmt, std::forward<Args>(arg)...);
        
        return SqliteStmt(stmt);
    }

    template <typename... Args>
    SqliteStmt queryString(const std::string &fmt, Args &&...args) {
        return queryString(fmt.data(), std::forward<Args>(args)...);
    }

    SqliteStmt queryString(const char *fmt) {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(_db.get(), fmt, -1, &stmt, nullptr) != SQLITE_OK) {
            throw SqliteException(sqlite3_sql(stmt), sqlite3_errmsg(_db.get()));
        }
        return SqliteStmt(stmt);
    }

    SqliteStmt queryString(const std::string &fmt) {
        return queryString(fmt.data());
    }

private:
    template <typename List, typename Fmt, typename... Args>
    int64_t queryList(int64_t &rowId, std::vector<List> &ret, Fmt &&fmt, Args &&...arg) {
        auto stmt = queryString(std::forward<Fmt>(fmt), std::forward<Args>(arg)...);
        DebugL << "Expanded sql: " << expandedSQL(stmt.get());

        ret.clear();
        int columnCount = sqlite3_column_count(stmt.get());

        int rc = 0;
        while ((rc = doQuery(stmt.get())) == SQLITE_ROW) {
            // SELECT
            ret.emplace_back();
            auto& back = ret.back();
            for (int i = 0; i < columnCount; ++i) {
                const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), i));
                back.emplace_back(value ? value : "");
            }
        }
        if (rc != SQLITE_DONE) {
            throw SqliteException(expandedSQL(stmt.get()), sqlite3_errmsg(_db.get()));

        }
        // INSERT/UPDATE/DELETE returns positive number
        // SELECT return 0;
        rowId = sqlite3_last_insert_rowid(_db.get());
        return sqlite3_changes(_db.get());
    }

    inline void check() {
        // Kiểm tra kết nối SQLite bằng cách thực hiện truy vấn đơn giản
        if (sqlite3_exec(_db.get(), "SELECT 1;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            throw SqliteException("sqlite3_ping", "Connection check failed");
        }
    }

    int doQuery(sqlite3_stmt *stmt) {
        return sqlite3_step(stmt);
    }

    int doQuery(const std::string &sql) {
        return doQuery(sql.data());
    }

    int doQuery(const char *sql) {
        return sqlite3_exec(_db.get(), sql, nullptr, nullptr, nullptr);
    }

    inline std::string expandedSQL(sqlite3_stmt* stmt) const {
        SqliteStringPtr ptr(sqlite3_expanded_sql(stmt));
        return ptr ? std::string(ptr.get()) : std::string();
    }

    // Helper to bind a single value
    inline int bindValue(sqlite3_stmt* stmt, int idx, int value) {
        return sqlite3_bind_int(stmt, idx, value);
    }
    inline int bindValue(sqlite3_stmt* stmt, int idx, int64_t value) {
        return sqlite3_bind_int64(stmt, idx, value);
    }
    inline int bindValue(sqlite3_stmt* stmt, int idx, double value) {
        return sqlite3_bind_double(stmt, idx, value);
    }
    inline int bindValue(sqlite3_stmt* stmt, int idx, const std::string& value) {
        return sqlite3_bind_text(stmt, idx, value.c_str(), value.size(), SQLITE_TRANSIENT);
    }
    inline int bindValue(sqlite3_stmt* stmt, int idx, const char* value) {
        return sqlite3_bind_text(stmt, idx, value, value ? std::strlen(value) : 0, SQLITE_TRANSIENT);
    }

    // Helper to bind all values (C++11 fold emulation)
    inline int bindAllImpl(sqlite3_stmt*, int) {
        return SQLITE_OK;
    }

    template<typename T, typename... Args>
    int bindAllImpl(sqlite3_stmt* stmt, int idx, T&& value, Args&&... args) {
        int rc = bindValue(stmt, idx, std::forward<T>(value));
        if (rc != SQLITE_OK) return rc;
        return bindAllImpl(stmt, idx + 1, std::forward<Args>(args)...);
    }

    template<typename... Args>
    int bindAll(sqlite3_stmt* stmt, Args&&... args) {
        return bindAllImpl(stmt, 1, std::forward<Args>(args)...);
    }

    template<typename Iter>
    int bindAll(sqlite3_stmt* stmt, Iter begin, Iter end) {
        int idx = 1;
        for (auto it = begin; it != end; ++it) {
            int rc = bindValue(stmt, idx, *it);
            if (rc != SQLITE_OK) return rc;
            ++idx;
        }
        return SQLITE_OK;
    }

private:
    SqliteDB _db;
};

} // namespace toolkit

#endif // SQL_SQLITECONNECTION_H