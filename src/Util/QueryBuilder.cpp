#include "QueryBuilder.h"

using namespace toolkit;

QueryBuilder::QueryBuilder() : _limit(-1), _offset(-1) {}

QueryBuilder& QueryBuilder::select(const std::vector<std::string>& columns) {
    _type = Type::SELECT;
    _selectColumns = columns;
    return *this;
}

QueryBuilder& QueryBuilder::from(const std::string& table) {
    _table = table;
    return *this;
}

QueryBuilder& QueryBuilder::update(const std::string& table) {
    _type = Type::UPDATE;
    _table = table;
    return *this;
}

QueryBuilder& QueryBuilder::set(const std::vector<std::pair<std::string, std::string>>& keyValues) {
    _updateSet.insert(_updateSet.end(), keyValues.begin(), keyValues.end());
    return *this;
}

QueryBuilder& QueryBuilder::insertInto(const std::string& table) {
    _type = Type::INSERT;
    _table = table;
    return *this;
}

QueryBuilder& QueryBuilder::values(const std::vector<std::pair<std::string, std::string>>& keyValues) {
    for (const auto& pair : keyValues) {
        _insertColumns.push_back(pair.first);
        _insertValues.push_back(pair.second);
    }
    return *this;
}

QueryBuilder& QueryBuilder::deleteFrom(const std::string& table) {
    _type = Type::DELETE;
    _table = table;
    return *this;
}

QueryBuilder& QueryBuilder::where(const std::string& condition, const std::vector<std::string>& params) {
    _whereClause = condition;
    _whereParams = params;
    return *this;
}

QueryBuilder& QueryBuilder::join(const std::string& clause) {
    _joinClauses.push_back("JOIN " + clause);
    return *this;
}

QueryBuilder& QueryBuilder::leftJoin(const std::string& clause) {
    _joinClauses.push_back("LEFT JOIN " + clause);
    return *this;
}

QueryBuilder& QueryBuilder::rightJoin(const std::string& clause) {
    _joinClauses.push_back("RIGHT JOIN " + clause);
    return *this;
}

QueryBuilder& QueryBuilder::groupBy(const std::string& clause) {
    _groupByClause = clause;
    return *this;
}

QueryBuilder& QueryBuilder::having(const std::string& clause) {
    _havingClause = clause;
    return *this;
}

QueryBuilder& QueryBuilder::orderBy(const std::string& clause) {
    _orderByClause = clause;
    return *this;
}

QueryBuilder& QueryBuilder::limit(int limit) {
    _limit = limit;
    return *this;
}

QueryBuilder& QueryBuilder::offset(int offset) {
    _offset = offset;
    return *this;
}

std::string QueryBuilder::build() const {
    std::ostringstream ss;
    switch (_type) {
        case Type::SELECT:
            ss << "SELECT ";
            if (!_selectColumns.empty()) {
                for (size_t i = 0; i < _selectColumns.size(); ++i) {
                    ss << _selectColumns[i];
                    if (i + 1 < _selectColumns.size()) ss << ", ";
                }
            } else {
                ss << "*";
            }
            ss << " FROM " << _table;
            break;

        case Type::UPDATE:
            ss << "UPDATE " << _table << " SET ";
            for (size_t i = 0; i < _updateSet.size(); ++i) {
                ss << _updateSet[i].first << " = ?";
                if (i + 1 < _updateSet.size()) ss << ", ";
            }
            break;

        case Type::INSERT:
            ss << "INSERT INTO " << _table << " (";
            for (size_t i = 0; i < _insertColumns.size(); ++i) {
                ss << _insertColumns[i];
                if (i + 1 < _insertColumns.size()) ss << ", ";
            }
            ss << ") VALUES (";
            for (size_t i = 0; i < _insertColumns.size(); ++i) {
                ss << "?";
                if (i + 1 < _insertColumns.size()) ss << ", ";
            }
            ss << ")";
            break;

        case Type::DELETE:
            ss << "DELETE FROM " << _table;
            break;

        default: 
            return ss.str();
        }

    for (const auto& join : _joinClauses) {
        ss << " " << join;
    }

    if (!_whereClause.empty()) ss << " WHERE " << _whereClause;
    if (!_groupByClause.empty()) ss << " GROUP BY " << _groupByClause;
    if (!_havingClause.empty()) ss << " HAVING " << _havingClause;
    if (!_orderByClause.empty()) ss << " ORDER BY " << _orderByClause;
    if (_limit >= 0) ss << " LIMIT " << _limit;
    if (_offset >= 0) ss << " OFFSET " << _offset;

    return ss.str();
}

const std::vector<std::string>& QueryBuilder::getParams() const {
    static std::vector<std::string> combined;
    combined.clear();
    switch (_type) {
        case Type::SELECT:
        case Type::DELETE:
            return _whereParams;
        case Type::UPDATE:
            for (size_t i = 0; i < _updateSet.size(); ++i) combined.push_back(_updateSet[i].second);
            combined.insert(combined.end(), _whereParams.begin(), _whereParams.end());
            return combined;
        case Type::INSERT:
            return _insertValues;
    }
    return combined;
}