#ifndef SQL_SQLITECONNECTION_H_
#define SQL_SQLITECONNECTION_H_

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

namespace toolkit
{

/**
 * Database exception class
 */
class SqliteException : public std::exception
{
public:
    SqliteException(const std::string &sql, const std::string &err)
    {
        _sql = sql;
        _err = err;
    }

    virtual const char *what() const noexcept
    {
        return _err.data();
    }

    const std::string &getSql() const
    {
        return _sql;
    }

private:
    std::string _sql;
    std::string _err;
};

/**
 * Sqlite connection
 */
class SqliteConnection
{
public:
    /**
     * Constructor
     * @param dbname Database name
     */
    SqliteConnection(const std::string &dbname)
    {
        if (sqlite3_open(dbname.c_str(), &_db) != SQLITE_OK)
        {
            std::string errorMsg = sqlite3_errmsg(_db);
            sqlite3_close(_db);
            throw SqliteException("sqlite3_open", errorMsg);
        }

        // Thiết lập chế độ hỗ trợ UTF-8
        if (sqlite3_exec(_db, "PRAGMA encoding = \"UTF-8\";", nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            std::string errorMsg = sqlite3_errmsg(_db);
            sqlite3_close(_db);
            throw SqliteException("sqlite3_exec encoding", errorMsg);
        }
    }

    ~SqliteConnection()
    {
        if (_db)
        {
            sqlite3_close(_db);
        }
    }

    /**
     * Execute SQL in printf style, no data returned
     * @param rowId Insert rowid when inserting
     * @param fmt printf type fmt
     * @param arg Variable argument list
     * @return Affected rows
     */
    template <typename Fmt, typename... Args>
    int64_t query(int64_t &rowId, Fmt &&fmt, Args &&...arg)
    {
        check();
        auto tmp = queryString(std::forward<Fmt>(fmt), std::forward<Args>(arg)...);

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(_db, tmp.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            throw SqliteException(tmp, std::string(sqlite3_errmsg(_db)));
        }

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            std::string errorMsg = sqlite3_errmsg(_db);
            sqlite3_finalize(stmt);
            throw SqliteException(tmp, errorMsg);
        }

        rowId = sqlite3_last_insert_rowid(_db);
        int64_t affectedRows = sqlite3_changes(_db);
        sqlite3_finalize(stmt);
        return affectedRows;
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
    int64_t query(int64_t &rowId, std::vector<std::vector<std::string>> &ret, Fmt &&fmt, Args &&...arg)
    {
        return queryList(rowId, ret, std::forward<Fmt>(fmt), std::forward<Args>(arg)...);
    }

    template <typename Fmt, typename... Args>
    int64_t query(int64_t &rowId, std::vector<std::list<std::string>> &ret, Fmt &&fmt, Args &&...arg)
    {
        return queryList(rowId, ret, std::forward<Fmt>(fmt), std::forward<Args>(arg)...);
    }

    template <typename Fmt, typename... Args>
    int64_t query(int64_t &rowId, std::vector<std::deque<std::string>> &ret, Fmt &&fmt, Args &&...arg)
    {
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
    int64_t query(int64_t &rowId, std::vector<Map> &ret, Fmt &&fmt, Args &&...arg)
    {
        check();
        auto tmp = queryString(std::forward<Fmt>(fmt), std::forward<Args>(arg)...);

        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(_db, tmp.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            throw SqliteException(tmp, std::string(sqlite3_errmsg(_db)));
        }

        ret.clear();
        int columnCount = sqlite3_column_count(stmt);

        // Lặp qua từng dòng kết quả
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            ret.emplace_back();
            auto& back = ret.back();
            for (int i = 0; i < columnCount; i++) {
                std::string columnName = sqlite3_column_name(stmt, i);
                const char *value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, i));
                back[std::string(columnName)] = (value ? value : "");
            }
        }

        rowId = sqlite3_last_insert_rowid(_db);
        int64_t affectedRows = sqlite3_changes(_db);
        sqlite3_finalize(stmt);
        return affectedRows;
    }

    std::string escape(const std::string &str)
    {
        std::string escaped;
        for (char c : str)
        {
            if (c == '\'')
            {
                escaped += "''"; // SQLite sử dụng hai dấu nháy đơn để escape
            }
            else
            {
                escaped += c;
            }
        }
        return escaped;
    }

    template <typename... Args>
    static std::string queryString(const char *fmt, Args &&...arg)
    {
        char *ptr_out = nullptr;
        if (asprintf(&ptr_out, fmt, arg...) > 0 && ptr_out)
        {
            std::string ret(ptr_out);
            free(ptr_out);
            return ret;
        }
        return "";
    }

    template <typename... Args>
    static std::string queryString(const std::string &fmt, Args &&...arg)
    {
        return queryString(fmt.data(), std::forward<Args>(arg)...);
    }

    static const char *queryString(const char *fmt) { return fmt; }

    static const std::string &queryString(const std::string &fmt) { return fmt; }

private:
    template <typename List, typename Fmt, typename... Args>
    int64_t queryList(int64_t &rowId, std::vector<List> &ret, Fmt &&fmt, Args &&...arg)
    {
        sqlite3_stmt *stmt;
        std::string tmp = queryString(std::forward<Fmt>(fmt), std::forward<Args>(arg)...);
        if (sqlite3_prepare_v2(_db, tmp.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        {
            throw SqliteException(tmp, std::string(sqlite3_errmsg(_db)));
        }

        ret.clear();
        int result;
        int columnCount = sqlite3_column_count(stmt);

        while ((result = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            ret.emplace_back();
            auto &back = ret.back();
            for (int i = 0; i < columnCount; i++)
            {
                const char *data = reinterpret_cast<const char *>(sqlite3_column_text(stmt, i));
                back.emplace_back(data ? data : ""); // Tránh nullptr
            }
        }

        if (result != SQLITE_DONE)
        {
            throw SqliteException("sqlite3_step", std::string(sqlite3_errmsg(_db)));
        }

        rowId = sqlite3_last_insert_rowid(_db);
        int64_t affectedRows = sqlite3_changes(_db);

        sqlite3_finalize(stmt);
        return affectedRows;
    }

    inline void check()
    {
        if (!_db)
        {
            throw SqliteException("SqliteConnection::check", "Database connection is not initialized");
        }

        // Kiểm tra kết nối SQLite bằng cách thực hiện truy vấn đơn giản
        if (sqlite3_exec(_db, "SELECT 1;", nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            throw SqliteException("SqliteConnection::check", "Connection check failed");
        }
    }

private:
    sqlite3 *_db;
};

} /* namespace toolkit */
#endif /* SQL_SQLITECONNECTION_H_ */
