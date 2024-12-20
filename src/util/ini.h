#ifndef _INI_H_
#define _INI_H_

#include <cctype>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "utility.h"
#include "file.h"

namespace xkernel {

template <typename key, typename variant>
class IniBasic : public std::map<key, variant> {
public:
    void parse(const std::string& text) {
        std::vector<std::string> lines = tokenize(text, "\n");
        std::string symbol, tag;
        
        for (auto& line : lines) {
            line = StringUtil::trim(line);  // 去除行首尾空白
            // 忽略空行和注释行（以 ; 或 # 开头）
            if (line.empty() || line.front() == ';' || line.front() == '#') {
                continue;
            }
            // 处理节名 [section]
            if (line.size() >= 3 && line.front() == '[' && line.back() == ']') {
                tag = StringUtil::trim(line.substr(1, line.size() - 2));
            } else {
                // 处理键值对 key=value
                auto at = line.find('=');
                symbol = StringUtil::trim(tag + "." + line.substr(0, at));  // 生成键名，section.key格式
                // 存储键值对
                (*this)[symbol] = (at == std::string::npos ? std::string() : StringUtil::trim(line.substr(at + 1)));
            }
        }
    }

    void parseFile(const std::string& file_name = ExeFile::exePath() + ".ini") {
        std::ifstream in(file_name, std::ios::in | std::ios::binary | std::ios::ate);
        if (!in.good()) {
            throw std::invalid_argument("Invalid ini file: " + file_name);
        }

        auto size = in.tellg();
        in.seekg(0, std::ios::beg);
        std::string buf;
        buf.resize(size);
        in.read((char*)buf.data(), size);
        parse(buf);
    }

    std::string dump(const std::string& header = "; auto-generated by INI class {",
                     const std::string& footer = "; } ---") const {
        const std::string front(header + (header.empty() ? "" : "\r\n")), output, tag;
        std::vector<std::string> kv;

        for (auto& pr : *this) {
            auto pos = pr.first.find('.');  // 处理section.key格式
            if (pos == std::string::npos) {
                kv = {"", pr.first};
            } else {
                kv = {pr.first.substr(0, pos), pr.first.substr(pos + 1)};
            }

            if (kv[0].empty()) {
                front += kv[1] + "=" + pr.second + "\r\n";
                continue;
            }
            // 添加新节名
            if (tag != kv[0]) {
                output += "\r\n[" + (tag = kv[0]) + "]\r\n";
            }
            output += kv[1] + "=" + pr.second + "\r\n";
        }
        return front + output + "\r\n" + footer + (footer.empty() ? "" : "\r\n");
    }

    void dumpFile(const std::string& file_name = ExeFile::exePath() + ".ini") {
        std::ofstream out(file_name, std::ios::out | std::ios::binary | std::ios::trunc);
        auto dump = dump();
        out.write(dump.data(), dump.size());
    }

    static IniBasic& Instance();

private:
    std::vector<std::string> tokenize(const std::string& self, const std::string& chars) const {
        std::vector<std::string> tokens(1);
        std::string map(256, '\0');
        for (char ch : chars) {
            map[(uint8_t)ch] = '\1';  // 标记分隔符位置
        }

        for (char ch : self) {
            if (!map.at(uint8_t(ch))) {
                tokens.back().push_back(ch);  // 如果不是分隔符，则添加到当前token中
            } else if (tokens.back().size()) {
                tokens.push_back(std::string());  // 添加存储下一个token的空串
            }
        }

        while (tokens.size() && tokens.back().empty()) {
            tokens.pop_back();  // 移除末尾空token
        }
        return tokens;
    }
};

// variant 类：用于存储不同类型的值
struct variant : public std::string {
    // 从数值类型构造
    template <typename T>
    variant(const T& t) :std::string(std::to_string(t)) {}
    // 从字符数组构造, 编译时确定长度
    template <size_t N>
    variant(const char (&s)[N]) : std::string(s, N) {}
    variant(const char* cstr) : std::string(cstr) {}  // 从char*构造，运行时确定长度
    variant(const std::string& other = std::string()) : std::string(other) {}  // 从string构造

    template <typename T>
    operator T() const {
        return as<T>();
    }

    template <typename T>
    bool operator==(const T& t) const {
        return 0 == this->compare(variant(t));
    }
    
    bool operator==(const char* t) const { return this->compare(t) == 0; }

    template <typename T>
    typename std::enable_if<!std::is_class<T>::value, T>::type as() const {
        return as_default<T>();
    }

    template <typename T>
    typename std::enable_if<std::is_class<T>::value, T>::type as() const {
        return T((const std::string&)* this);
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

using mIni = IniBasic<std::string, variant>;

}  // namespace xkernel

#endif