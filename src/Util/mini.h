#ifndef UTIL_MINI_H
#define UTIL_MINI_H

#include <cctype>
#include <map>
#include <string>
#include <vector>
#include <fstream>
#include "util.h"

namespace toolkit {

// Writes a temporary file next to fileName and renames it over fileName, so a crash or a full disk
// never leaves a truncated ini file behind; concurrent calls are serialized.
bool saveIniFile(const std::string &fileName, const std::string &content);

template<typename variant>
class mINI_basic : public std::map<std::string, variant> {
    // Public API : existing map<> interface plus following methods
public:
    void parse(const std::string &text) {
        // reset, split lines and parse
        std::vector<std::string> lines = tokenize(text, "\n");
        std::string tag, comment;
        _sort.clear();
        _sort.reserve(lines.size());
        for (auto &line : lines) {
            // trim blanks
            line = trim(line);
            // split line into tokens and parse tokens
            if (line.empty() || line.front() == ';' || line.front() == '#') {
                comment.append(line + "\r\n");
                continue;
            }
            if (line.size() >= 3 && line.front() == '[' && line.back() == ']') {
                tag = trim(line.substr(1, line.size() - 2));
                // [field] Comment
                _comment.emplace(tag, std::move(comment));
                comment.clear();
            } else {
                const auto at = line.find('=');
                std::string symbol = trim(tag + "." + line.substr(0, at));
                // field.key Comment
                _comment.emplace(symbol, std::move(comment));
                comment.clear();
                _sort.emplace_back(symbol);
                (*this)[symbol] = (at == std::string::npos ? std::string() : trim(line.substr(at + 1)));
            }
        }
    }

    void parseFile(const std::string &fileName = exePath() + ".ini") {
        std::ifstream in(fileName, std::ios::in | std::ios::binary | std::ios::ate);
        if (!in.good()) {
            throw std::invalid_argument("Invalid ini file: " + fileName);
        }
        const auto size = in.tellg();
        in.seekg(0, std::ios::beg);
        std::string buf;
        buf.resize(size);
        in.read(const_cast<char *>(buf.data()), size);
        parse(buf);
    }

    // skip_default: leave out the keys whose value equals the one recorded by markDefaults()
    std::string dump(const std::string &header = "",
                     const std::string &footer = "",
                     bool skip_default = false) const {
        std::string output;
        std::vector<std::string> kv;
        auto copy = *this;
        output += dump_l(copy, _sort, skip_default);

        if (!copy.empty()) {
            std::vector<std::string> sort;
            for (auto &pr : copy) {
                sort.emplace_back(pr.first);
            }
            output += dump_l(copy, sort, skip_default);
        }

        return header + (header.empty() ? "" : "\r\n") + output + "\r\n" + footer + (footer.empty() ? "" : "\r\n");
    }

    // Only the values differing from markDefaults() are written; the file is replaced atomically.
    void dumpFile(const std::string &fileName = exePath() + ".ini") const {
        saveIniFile(fileName, dump("", "", true));
    }

    void updateFrom(const mINI_basic &that) {
        _sort = that._sort;
        _comment = that._comment;
    }

    // Records the current values as the defaults left out by dumpFile(); call it once all defaults are
    // registered and before the ini file is loaded. Without it dumpFile() writes every key.
    void markDefaults() {
        _default = *this;
    }

    static mINI_basic &Instance();

private:
    static std::vector<std::string> tokenize(const std::string &self, const std::string &chars) {
        std::vector<std::string> tokens(1);
        std::string map(256, '\0');
        for (const char ch : chars) {
            map[static_cast<uint8_t>(ch)] = '\1';
        }
        for (const char ch : self) {
            if (!map.at(static_cast<uint8_t>(ch))) {
                tokens.back().push_back(ch);
            } else if (tokens.back().size()) {
                tokens.push_back(std::string());
            }
        }
        while (tokens.size() && tokens.back().empty()) {
            tokens.pop_back();
        }
        return tokens;
    }

    std::string getComment(const std::string &tag) const {
        const auto it = _comment.find(tag);
        if (it == _comment.end()) {
            return "";
        }
        return it->second;
    }

    bool isDefault(const std::string &key, const std::string &value) const {
        if (_default.empty()) {
            return false;
        }
        const auto it = _default.find(key);
        // A key without a default (unknown or never registered) is kept unless it is empty
        return it == _default.end() ? value.empty() : static_cast<const std::string &>(it->second) == value;
    }

    std::string dump_l(std::map<std::string, variant> &copy, const std::vector<std::string> &sort, bool skip_default) const {
        std::string output, tag;
        std::vector<std::string> kv;
        for (auto &key : sort) {
            auto pos = key.find('.');
            if (pos == std::string::npos) {
                kv = { "", key };
            } else {
                kv = { key.substr(0, pos), key.substr(pos + 1) };
            }
            auto &value = copy[key];
            if (skip_default && isDefault(key, value)) {
                copy.erase(key);
                continue;
            }
            if (kv[0].empty()) {
                auto comment = getComment("." + kv[1]);
                if (!comment.empty()) {
                    output += comment;
                }
                output += kv[1] + "=" + value + "\r\n";
                copy.erase(key);
                continue;
            }
            if (tag != kv[0]) {
                auto comment = getComment(kv[0]);
                if (!comment.empty()) {
                    output += comment;
                } else {
                    output += "\r\n";
                }
                output += "[" + (tag = kv[0]) + "]\r\n";
            }
            auto comment = getComment(tag + "." + kv[1]);
            if (!comment.empty()) {
                output += comment;
            }
            output += kv[1] + "=" + value + "\r\n";
            copy.erase(key);
        }
        return output;
    }

private:
    std::vector<std::string> _sort;
    std::map<std::string, std::string> _comment;
    std::map<std::string, variant> _default;
};

//  handy variant class as key/values
struct variant : public std::string {
    template<typename T>
    variant(const T &t) :
            std::string(std::to_string(t)) {
    }

    template<size_t N>
    variant(const char (&s)[N]) :
            std::string(s, N) {
    }

    variant(const char *cstr) :
            std::string(cstr) {
    }

    variant(const std::string &other = std::string()) :
            std::string(other) {
    }

    template <typename T>
    operator T() const {
        return as<T>();
    }

    template<typename T>
    bool operator==(const T &t) const {
        return 0 == this->compare(variant(t));
    }

    bool operator==(const char *t) const {
        return this->compare(t) == 0;
    }

    template <typename T>
    typename std::enable_if<!std::is_class<T>::value, T>::type as() const {
        return as_default<T>();
    }

    template <typename T>
    typename std::enable_if<std::is_class<T>::value, T>::type as() const {
        return T(static_cast<const std::string &>(*this));
    }

private:
    template <typename T>
    T as_default() const {
        T t;
        std::stringstream ss;
        return ss << *this && ss >> t ? t : T();
    }
};

template <>
bool variant::as<bool>() const;

template <>
uint8_t variant::as<uint8_t>() const;

using mINI = mINI_basic<variant>;

}  // namespace toolkit
#endif //UTIL_MINI_H

