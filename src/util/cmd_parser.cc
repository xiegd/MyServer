#include "cmd_parser.h"

#include <vector>
#include <map>
#include <getopt.h>
#include <cstring>

#include "ini.h"

namespace xkernel {

static onceToken s_token([]() {
    RegistCmd(Exit);
    RegistCmd(Quit);
    RegistCmd(Help);
    RegistCmd(Clear);
});

///////////////////////// Option /////////////////////////

Option::Option(char short_opt, const char* long_opt, ArgType type,
               const char* default_value, bool must_exist, const char* des,
               const OptionHandler& cb) {
    short_opt_ = short_opt;
    long_opt_ = long_opt;
    type_ = type;
    if (type != ArgType::None) {
        if (default_value) {
            default_value_ = std::make_shared<std::string>(default_value);
        }
        if (!default_value_ && must_exist) {
            must_exist_ = true;
        }
    }
    des_ = des;
    cb_ = cb;
}

bool Option::operator()(const std::shared_ptr<std::ostream>& stream, const std::string& arg) {
    return cb_ ? cb_(stream, arg) : true;
}

///////////////////////// OptionParser /////////////////////////

OptionParser::OptionParser(const OptionCompleted& cb, bool enable_empty_args) {
    on_completed_ = cb;
    enable_empty_args_ = enable_empty_args;
    helper_ = Option(
        'h', "help", Option::ArgType::None, nullptr, false, "", 
        [this](const std::shared_ptr<std::ostream>& stream, const std::string& arg) -> bool {
            static const char* argsType[] = {"no arg", "has arg", "optional arg"};
            static const char* mustExist[] = {"optional", "required"};
            static std::string defaultPrefix = "default: ";
            static std::string defaultNull = "null";

            std::stringstream printer;
            size_t maxlen_long_opt = 0;
            auto maxlen_default = defaultNull.size();

            for (auto& pr : map_options_) {
                auto& opt = pr.second;
                if (opt.long_opt_.size() > maxlen_long_opt) {
                    maxlen_long_opt = opt.long_opt_.size();
                }
                if (opt.default_value_) {
                    if (opt.default_value_->size() > maxlen_default) {
                        maxlen_default = opt.default_value_->size();
                    }
                }
            }

            for (auto& pr : map_options_) {
                auto& opt = pr.second;
                if (opt.short_opt_) {
                    printer << "  -" << opt.short_opt_ << "  --" << opt.long_opt_;
                } else {
                    printer << "   " << " " << "  --" << opt.long_opt_;
                }
                for (size_t i = 0; i < maxlen_long_opt - opt.long_opt_.size(); ++i) {
                    printer << " ";
                }
                printer << "  " << argsType[static_cast<int>(opt.type_)];
                std::string defaultValue = defaultNull;
                if (opt.default_value_) {
                    defaultValue = *opt.default_value_;
                }
                printer << "  " << defaultPrefix << defaultValue;
                for (size_t i = 0; i < maxlen_default - defaultValue.size(); ++i) {
                    printer << " ";
                }
                printer << "  " << mustExist[opt.must_exist_];
                printer << "  " << opt.des_ << std::endl;
            }
            throw std::invalid_argument(printer.str());
        });
    (*this) << helper_;
}

OptionParser& OptionParser::operator<<(Option&& option) {
    int index = 0xFF + map_options_.size();
    if (option.short_opt_) {
        map_char_index_.emplace(option.short_opt_, index);
    }
    map_options_.emplace(index, std::forward<Option>(option));
    return *this;
}

OptionParser& OptionParser::operator<<(const Option& option) {
    int index = 0xFF + map_options_.size();
    if (option.short_opt_) {
        map_char_index_.emplace(option.short_opt_, index);
    }
    map_options_.emplace(index, option);
    return *this;
}

void OptionParser::delOption(const char* key) {
    for (auto& pr : map_options_) {
        if (pr.second.long_opt_ == key) {
            if (pr.second.short_opt_) {
                map_char_index_.erase(pr.second.short_opt_);
            }
            map_options_.erase(pr.first);
            break;
        }
    }
}

void OptionParser::operator()(mIni& all_args, int argc, char* argv[], 
                              const std::shared_ptr<std::ostream>& stream) {
    std::vector<struct option> vec_long_opt;
    std::string str_short_opt;
    // 使用Option填充struct option
    do {
        struct option tmp;
        for (auto& pr : map_options_) {
            auto& opt = pr.second;
            tmp.name = opt.long_opt_.data();
            tmp.has_arg = static_cast<int>(opt.type_);
            tmp.flag = nullptr;
            tmp.val = pr.first;
            vec_long_opt.emplace_back(tmp);
            if (!opt.short_opt_) {
                continue;
            }
            str_short_opt.push_back(opt.short_opt_);
            switch (opt.type_) {
                case Option::ArgType::Required:
                    str_short_opt.append(":");
                    break;
                case Option::ArgType::Optional:
                    str_short_opt.append("::");
                    break;
                default:
                    break;
            }
        }
        tmp.flag = 0;
        tmp.name = 0;
        tmp.has_arg = 0;
        tmp.val = 0;
        vec_long_opt.emplace_back(tmp);  // 空选项，作为结束标志
    } while (0);

    static std::mutex s_mtx_opt;
    std::lock_guard<std::mutex> lock(s_mtx_opt);

    int index;
    optind = 0;
    opterr = 0;
    // 解析命令行参数, 保存到all_args
    while ((index = getopt_long(argc, argv, &str_short_opt[0], &vec_long_opt[0], nullptr)) != -1) {
        std::stringstream ss;
        ss << "  未识别的选项，输入\"-h\"获取帮助";
        if (index < 0xFF) {
            auto it = map_char_index_.find(index);
            if (it == map_char_index_.end()) {
                throw std::invalid_argument(ss.str());
            }
            index = it->second;
        }
        auto it = map_options_.find(index);
        if (it == map_options_.end()) {
            throw std::invalid_argument(ss.str());
        }

        auto& opt = it->second;
        auto pr = all_args.emplace(opt.long_opt_, optarg ? optarg : "");
        if (!opt(stream, pr.first->second)) {
            return ;
        }
        optarg = nullptr;
    }
    // 补充没有显式设置的选项，设置为默认值
    for (auto& pr : map_options_) {
        if (pr.second.default_value_ && 
            all_args.find(pr.second.long_opt_) == all_args.end()) {
            all_args.emplace(pr.second.long_opt_, *pr.second.default_value_);
        }
    }
    for (auto& pr : map_options_) {
        if (pr.second.must_exist_) {
            if (all_args.find(pr.second.long_opt_) == all_args.end()) {
                std::stringstream ss;
                ss << "  参数\"" << pr.second.long_opt_
                   << "\"必须提供,输入\"-h\"选项获取帮助";
                throw std::invalid_argument(ss.str());
            }
        }
    }
    if (all_args.empty() && map_options_.size() > 1 && !enable_empty_args_) {
        helper_(stream, "");
        return ;
    }
    if (on_completed_) {
        on_completed_(stream, all_args);
    }
}

///////////////////////// Cmd /////////////////////////

const char* Cmd::description() const { return "description"; }

void Cmd::operator()(int argc, char* argv[], const std::shared_ptr<std::ostream>& ostream) {
    this->clear();
    std::shared_ptr<std::ostream> cout_ptr(&std::cout, [](std::ostream*) {});
    (*parser_)(*this, argc, argv, ostream ? ostream : cout_ptr);
}

bool Cmd::hasKey(const char* key) { return this->find(key) != this->end(); }

std::vector<variant> Cmd::splitedVal(const char* key, const char* delim) {
    std::vector<variant> ret;
    auto& val = (*this)[key];
    split(val, delim, ret);
    return ret;
}

void Cmd::delOption(const char* key) {
    if (parser_) {
        parser_->delOption(key);
    }
}

void Cmd::split(const std::string& s, const char* delim, std::vector<variant>& ret) {
    size_t last = 0;
    auto index = s.find(delim, last);
    while (index != std::string::npos) {
        if (index - last > 0) {
            ret.push_back(s.substr(last, index - last));
        }
        last = index + strlen(delim);
        index = s.find(delim, last);
    }
    if (s.size() - last > 0) {
        ret.push_back(s.substr(last));
    }
}

///////////////////////// CmdRegister /////////////////////////

CmdRegister& CmdRegister::Instance() {
    static CmdRegister instance;
    return instance;
}

void CmdRegister::clear() {
    std::lock_guard<decltype(mtx_)> lck(mtx_);
    cmd_map_.clear();
}

void CmdRegister::registCmd(const char* name, const std::shared_ptr<Cmd>& cmd) {
    std::lock_guard<decltype(mtx_)> lck(mtx_);
    cmd_map_.emplace(name, cmd);
}

void CmdRegister::unregistCmd(const char* name) {
    std::lock_guard<decltype(mtx_)> lck(mtx_);
    cmd_map_.erase(name);
}

std::shared_ptr<Cmd> CmdRegister::operator[](const char* name) {
    std::lock_guard<decltype(mtx_)> lck(mtx_);
    auto it = cmd_map_.find(name);
    if (it == cmd_map_.end()) {
        throw std::invalid_argument(std::string("CMD not existed: ") + name);
    }
    return it->second;
}

void CmdRegister::operator()(const char* name, int argc, char* argv[],
                             const std::shared_ptr<std::ostream>& stream) {
    auto cmd = (*this)[name];
    if (!cmd) {
        throw std::invalid_argument(std::string("CMD not existed: ") + name);
    }
    (*cmd)(argc, argv, stream);
}

void CmdRegister::printHelp(const std::shared_ptr<std::ostream>& stream_tmp) {
    auto stream = stream_tmp;
    if (!stream) {
        stream.reset(&std::cout, [](std::ostream*) {});
    }

    std::lock_guard<decltype(mtx_)> lck(mtx_);
    size_t max_len = 0;
    for (auto& pr : cmd_map_) {
        if (pr.first.size() > max_len) {
            max_len = pr.first.size();
        }
    }
    for (auto& pr : cmd_map_) {
        (*stream) << "  " << pr.first;
        for (size_t i = 0; i < max_len - pr.first.size(); ++i) {
            (*stream) << " ";
        }
        (*stream) << "  " << pr.second->description() << std::endl;
    }
}

void CmdRegister::operator()(const std::string& line, const std::shared_ptr<std::ostream>& stream) {
    if (line.empty()) {
        return ;
    }
    std::vector<char*> argv;
    size_t argc = getArgs(const_cast<char*>(line.data()), argv);
    if (argc == 0) {
        return ;
    }
}

size_t CmdRegister::getArgs(char* buf, std::vector<char*>& argv) {
    size_t argc = 0;
    bool start = false;
    auto len = strlen(buf);
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != ' ' && buf[i] != '\t' && buf[i] != '\r' && buf[i] != '\n') {
            if (!start) {
                start = true;
                if (argv.size() < argc + 1) {
                    argv.resize(argc + 1);
                }
                argv[argc++] = buf + i;
            }
        } else {
            buf[i] = '\0';
            start = false;
        }
    }
    return argc;
}

///////////////////////// CmdHelp /////////////////////////

CmdHelp::CmdHelp() {
    parser_ = std::make_shared<OptionParser>(
        [](const std::shared_ptr<std::ostream>& stream, mIni&) {
            CmdRegister::Instance().printHelp(stream);
        }
    );
}

const char* CmdHelp::description() const { return "打印帮助信息"; }

///////////////////////// CmdExit /////////////////////////

CmdExit::CmdExit() {
    parser_ = std::make_shared<OptionParser>(
        [](const std::shared_ptr<std::ostream>&, mIni& args) {
            throw ExitException();
        }
    );
}

const char* CmdExit::description() const { return "退出shell"; }

///////////////////////// CmdClear /////////////////////////

CmdClear::CmdClear() {
    parser_ = std::make_shared<OptionParser>(
        [this](const std::shared_ptr<std::ostream>& stream, mIni& args) {
            clear(stream);
        }
    );
}

const char* CmdClear::description() const { return "clear screen"; }

void CmdClear::clear(const std::shared_ptr<std::ostream>& stream) {
    (*stream) << "\x1b[2J\x1b[H";
    stream->flush();
}

}  // namespace xkernel