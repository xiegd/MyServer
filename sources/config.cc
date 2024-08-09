#include "sylar/config.h"
#include "sylar/env.h"
#include "sylar/util.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 查找是否有name对应的配置项
ConfigVarBase::ptr Config::LookupBase(const std::string& name) {
    RWMutexType::ReadLock lock(GetMutex());
    auto it = GetDatas().find(name);
    return it == GetDatas().end() ? nullptr : it->second;
}

//"A.B", 10
//A:
//  B: 10
//  C: str
// 递归调用ListAllMember，DFS遍历YAML节点树并将节点加入到output
static void ListAllMember(const std::string& prefix,
                          const YAML::Node& node,
                          std::list<std::pair<std::string, const YAML::Node> >& output) {
    if(prefix.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678")
            != std::string::npos) {
        SYLAR_LOG_ERROR(g_logger) << "Config invalid name: " << prefix << " : " << node;
        return;
    }
    output.push_back(std::make_pair(prefix, node));
    if(node.IsMap()) {
        for(auto it = node.begin();
                it != node.end(); ++it) {
            ListAllMember(prefix.empty() ? it->first.Scalar()
                    : prefix + "." + it->first.Scalar(), it->second, output);
        }
    }
}

// 使用YAML::Node初始化配置模块
void Config::LoadFromYaml(const YAML::Node& root) {
    std::list<std::pair<std::string, const YAML::Node> > all_nodes;
    ListAllMember("", root, all_nodes);

    for(auto& i : all_nodes) {
        std::string key = i.first;
        if(key.empty()) {
            continue;
        }

        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        ConfigVarBase::ptr var = LookupBase(key);  // 查找是否有这个配置项

        if(var) {
            // YAML-cpp中定义的方法IsScalar，检查是否为标量
            if(i.second.IsScalar()) {
                var->fromString(i.second.Scalar());  // 获取YAML::Node对应的标量值的string形式，
            } else {
                std::stringstream ss;
                ss << i.second;  // 对于非标量值直接转换为字符串可能会丢失信息
                var->fromString(ss.str());
            }
        }
    }
}

static std::map<std::string, uint64_t> s_file2modifytime;  // 存储每个配置项的修改时间
static sylar::Mutex s_mutex;

// 遍历指定路径下的YAML配置文件，加载配置文件，更新配置信息, force是否强制加载
void Config::LoadFromConfDir(const std::string& path, bool force) {
    std::string absoulte_path = sylar::EnvMgr::GetInstance()->getAbsolutePath(path);  // GetInstance获取一个相应单例的类实例
    std::vector<std::string> files;
    FSUtil::ListAllFile(files, absoulte_path, ".yml");

    for(auto& i : files) {
        {  // 通过限制st的作用域，确保在代码结束时，st会自动销毁
            struct stat st;  // 在<sys/stat.h>中声明，存储文件状态信息
            lstat(i.c_str(), &st);  // lstat系统调用将对应文件的状态信息填充到stat结构体中
            sylar::Mutex::Lock lock(s_mutex);
            // 如果不需要强制加载，且文件修改时间和记录的修改时间相同则跳过
            if(!force && s_file2modifytime[i] == (uint64_t)st.st_mtime) {
                continue;
            }
            s_file2modifytime[i] = st.st_mtime;  // 更新修改时间
        }
        try {
            YAML::Node root = YAML::LoadFile(i);  
            LoadFromYaml(root);  // 加载配置
            SYLAR_LOG_INFO(g_logger) << "LoadConfFile file="
                << i << " ok";
        } catch (...) {
            SYLAR_LOG_ERROR(g_logger) << "LoadConfFile file="
                << i << " failed";
        }
    }
}

// 遍历所有的配置项，并根据传入的可调用对象（包括仿函数，lambda表达式等）执行相应的操作
void Config::Visit(std::function<void(ConfigVarBase::ptr)> cb) {
    RWMutexType::ReadLock lock(GetMutex());
    ConfigVarMap& m = GetDatas();
    for(auto it = m.begin();
            it != m.end(); ++it) {
        cb(it->second);
    }
}

}
