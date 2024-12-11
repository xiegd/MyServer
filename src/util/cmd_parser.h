#ifndef _CMD_PARSER_H_
#define _CMD_PARSER_H_

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ini.h"

namespace xkernel {

class Option {
public:
    using OptionHandler = std::function<
        bool(const std::shared_ptr<std::ostream>& stream, const std::string& arg)>;
    
    enum class ArgType {
        None = 0,
        Required = 1,
        Optional = 2,
    };

    Option() = default;
    Option(char short_opt, const char* long_opt, ArgType type,
           const char* default_value, bool must_exist, const char* des,
           const OptionHandler& cb);

    bool operator()(const std::shared_ptr<std::ostream>& stream, const std::string& arg);

private:
    friend class OptionParser;
    bool must_exist_ = false;
    char short_opt_;
    ArgType type_;
    std::string des_;
    std::string long_opt_;
    OptionHandler cb_;
    std::shared_ptr<std::string> default_value_;
};

class OptionParser {
public:
    using OptionCompleted = std::function<void(const std::shared_ptr<std::ostream>&, mIni&)>;

    OptionParser(const OptionCompleted& cb = nullptr, bool enable_empty_args = true);
    OptionParser& operator<<(Option&& option);
    OptionParser& operator<<(const Option& option);
    void delOption(const char* key);
    void operator()(mIni& all_args, int argc, char* argv[], const std::shared_ptr<std::ostream>& stream);

private:
    bool enable_empty_args_;
    Option helper_;
    std::map<char, int> map_char_index_;
    std::map<int, Option> map_options_;
    OptionCompleted on_completed_;
};

class Cmd : public mIni {
public:
    Cmd() = default;
    virtual ~Cmd() = default;
    virtual const char* description() const;
    void operator()(int argc, char* argv[], const std::shared_ptr<std::ostream>& stream = nullptr);
    bool hasKey(const char* key);
    std::vector<variant> splitedVal(const char* key, const char* delim = ":");
    void delOption(const char* key);

private:
    void split(const std::string& s, const char* delim, std::vector<variant>& ret);

protected:
    std::shared_ptr<OptionParser> parser_;
};

class CmdRegister {
public:
    static CmdRegister& Instance();
    void clear();
    void registCmd(const char* name, const std::shared_ptr<Cmd>& cmd);
    void unregistCmd(const char* name);
    std::shared_ptr<Cmd> operator[](const char* name);
    void operator()(const char* name, int argc, char* argv[], 
                    const std::shared_ptr<std::ostream>& stream = nullptr);
    void printHelp(const std::shared_ptr<std::ostream>& stream_tmp = nullptr);
    void operator()(const std::string& line, const std::shared_ptr<std::ostream>& stream = nullptr);

private:
    size_t getArgs(char* buf, std::vector<char*>& argv);

private:
    std::recursive_mutex mtx_;
    std::map<std::string, std::shared_ptr<Cmd>> cmd_map_;
};

class CmdHelp : public Cmd {
public:
    CmdHelp();
    const char* description() const override;
};

class ExitException : public std::exception {};

class CmdExit : public Cmd {
public:
    CmdExit();
    const char* description() const override;
};

#define CmdQuit CmdExit

class CmdClear : public Cmd {
public:
    CmdClear();
    ~CmdClear() override = default;
    const char* description() const override;

private:
    void clear(const std::shared_ptr<std::ostream>& stream);
};

#define GetCmd(name) (*(CmdRegister::Instance()[name]))  // 获取命令
#define CmdDo(name, ...) (*(CmdRegister::Instance()[name]))(__VA_ARGS__)  // 执行命令
#define RegistCmd(name) \
    CmdRegister::Instance().registCmd(#name, std::make_shared<Cmd##name>());  // 注册命令

}  // namespace xkernel

#endif