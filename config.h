#ifndef __SYLAR_CONFIG_H__
#define __SYLAR_CONFIG_H__

#include <memory>
#include <string>
#include <sstream>
#include <boost/lexical_cast.hpp>
#include <yaml-cpp/yaml.h>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>

#include "thread.h"
#include "log.h"
#include "util.h" 

namespace sylar {


// 配置变量的基类
class ConfigVarBase {
public:
    typedef std::shared_ptr<ConfigVarBase> ptr;
    
    ConfigVarBase(const std::string& name, const std::string& description = "")
        :m_name(name)
        ,m_description(description) {
        std::transform(m_name.begin(), m_name.end(), m_name.begin(), ::tolower);  // 将m_name转换为小写形式（STL算法），存放到m_name中
    }

    virtual ~ConfigVarBase() {}
 
    const std::string& getName() const { return m_name;}  // 返回配置参数名称
    const std::string& getDescription() const { return m_description;}  // 返回配置参数的描述
    virtual std::string toString() = 0;  // 转成字符串, 纯虚函数，在派生类中实现
    virtual bool fromString(const std::string& val) = 0;  // 从字符串初始化值, 纯虚函数，在派生类中实现
    virtual std::string getTypeName() const = 0;  // 返回配置参数值的类型名称, 纯虚函数，在派生类中实现

protected:
    std::string m_name;  // 配置参数的名称, 大小写不敏感
    std::string m_description;  // 配置参数的描述
};


// 类型转换模板类(F 源类型, T 目标类型), 仿函数, 实现基本类型和str的转换
template<class F, class T>
class LexicalCast {
public:
    // 重载()函数调用运算符，使得类的实例可以像函数一样被调用，const表示参数戙常量引用传递
    T operator()(const F& v) {
        return boost::lexical_cast<T>(v);
    }
};


// 类型转换模板类偏特化(YAML String 转换成 std::vector<T>)
template<class T>
class LexicalCast<std::string, std::vector<T> > {
public:
    std::vector<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);  // 将str转换为yaml格式的数据，存在node中
        typename std::vector<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];  // 下面创建了一个LexicalCast类型的临时变量，然后调用重载的`()`运算符
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));  // 使用仿函数实现基本类型to string
        }
        return vec;
    }
};


// 类型转换模板类偏特化(std::vector<T> 转换成 YAML String)
template<class T>
class LexicalCast<std::vector<T>, std::string> {
public:
    std::string operator()(const std::vector<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));  // 使用前面实现的仿函数，进行基本类型和string之间的转换
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// 类型转换模板类偏特化(YAML String 转换成 std::list<T>)
template<class T>
class LexicalCast<std::string, std::list<T> > {
public:
    std::list<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::list<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");  // 将ss设置为空串
            ss << node[i];  // 向ss中追加内容
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

// 类型转换模板类偏特化(std::list<T> 转换成 YAML String)
template<class T>
class LexicalCast<std::list<T>, std::string> {
public:
    std::string operator()(const std::list<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// 类型转换模板类偏特化(YAML String 转换成 std::set<T>)
template<class T>
class LexicalCast<std::string, std::set<T> > {
public:
    std::set<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::set<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

// 类型转换模板类偏特化(std::set<T> 转换成 YAML String)
template<class T>
class LexicalCast<std::set<T>, std::string> {
public:
    std::string operator()(const std::set<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// 类型转换模板类偏特化(YAML String 转换成 std::unordered_set<T>)
template<class T>
class LexicalCast<std::string, std::unordered_set<T> > {
public:
    std::unordered_set<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_set<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

// 类型转换模板类偏特化(std::unordered_set<T> 转换成 YAML String)
template<class T>
class LexicalCast<std::unordered_set<T>, std::string> {
public:
    std::string operator()(const std::unordered_set<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// 类型转换模板类偏特化(YAML String 转换成 std::map<std::string, T>)
template<class T>
class LexicalCast<std::string, std::map<std::string, T> > {
public:
    std::map<std::string, T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::map<std::string, T> vec;
        std::stringstream ss;
        for(auto it = node.begin();
                it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            vec.insert(std::make_pair(it->first.Scalar(),
                        LexicalCast<std::string, T>()(ss.str())));
        }
        return vec;
    }
};

// 类型转换模板类片特化(std::map<std::string, T> 转换成 YAML String)
template<class T>
class LexicalCast<std::map<std::string, T>, std::string> {
public:
    std::string operator()(const std::map<std::string, T>& v) {
        YAML::Node node(YAML::NodeType::Map);
        for(auto& i : v) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// 类型转换模板类片特化(YAML String 转换成 std::unordered_map<std::string, T>)
template<class T>
class LexicalCast<std::string, std::unordered_map<std::string, T> > {
public:
    std::unordered_map<std::string, T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_map<std::string, T> vec;
        std::stringstream ss;
        for(auto it = node.begin();
                it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            vec.insert(std::make_pair(it->first.Scalar(),
                        LexicalCast<std::string, T>()(ss.str())));
        }
        return vec;
    }
};

// 类型转换模板类片特化(std::unordered_map<std::string, T> 转换成 YAML String)
template<class T>
class LexicalCast<std::unordered_map<std::string, T>, std::string> {
public:
    std::string operator()(const std::unordered_map<std::string, T>& v) {
        YAML::Node node(YAML::NodeType::Map);
        for(auto& i : v) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};


// 配置参数模板子类,保存对应类型的参数值, T 参数的具体类型, std::string 为YAML格式的字符串
// 一个参数对应一个类实例，
template<class T, class FromStr = LexicalCast<std::string, T>
                ,class ToStr = LexicalCast<T, std::string> >  // 定义三个模板参数，后两个给出默认参数值
class ConfigVar : public ConfigVarBase {
public:
    typedef RWMutex RWMutexType;
    typedef std::shared_ptr<ConfigVar> ptr;
    typedef std::function<void (const T& old_value, const T& new_value)> on_change_cb;  // 定义回调函数, 函数接受两个参数，没有返回值

    // 通过参数名,参数值,描述构造ConfigVar, name 参数名称有效字符为[0-9a-z_.]
    ConfigVar(const std::string& name
            ,const T& default_value
            ,const std::string& description = "")
        :ConfigVarBase(name, description)
        ,m_val(default_value) {
    }
 
    // 将参数值转换成YAML String, 当转换失败抛出异常 (override，覆盖基类中的虚函数)
    std::string toString() override {
        try {
            //return boost::lexical_cast<std::string>(m_val);
            RWMutexType::ReadLock lock(m_mutex);
            return ToStr()(m_val);
        } catch (std::exception& e) {
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ConfigVar::toString exception "
                << e.what() << " convert: " << TypeToName<T>() << " to string"
                << " name=" << m_name;  // e.what(): 异常信息，TypeToName<T>(): 将对应类型名转换为str
        }
        return "";
    }

    // 从YAML String 转成参数的值, 当转换失败抛出异常 (覆盖基类中的纯虚函数)
    bool fromString(const std::string& val) override {
        try {
            setValue(FromStr()(val));
        } catch (std::exception& e) {
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ConfigVar::fromString exception "
                << e.what() << " convert: string to " << TypeToName<T>()
                << " name=" << m_name
                << " - " << val;
        }
        return false;
    }
 
    // 获取当前参数的值
    const T getValue() {
        RWMutexType::ReadLock lock(m_mutex);
        return m_val;
    }

    // 设置当前参数的值. 如果参数的值有发生变化,则通知对应的注册回调函数
    void setValue(const T& v) {
        {
            RWMutexType::ReadLock lock(m_mutex);
            if(v == m_val) {
                return;
            }
            // 遍历回调函数的容器，对每个回调函数进行调用，传入当前的m_val和v作为参数,
            for(auto& i : m_cbs) {
                i.second(m_val, v);  // old_value, new_value
            }
        }
        RWMutexType::WriteLock lock(m_mutex);
        m_val = v;
    }

    // 返回参数值的类型名称(typeinfo),(覆盖基类中的纯虚函数)
    std::string getTypeName() const override { return TypeToName<T>();}
    
    // 添加变化回调函数, 返回该回调函数对应的唯一id,用于删除回调
    uint64_t addListener(on_change_cb cb) {
        static uint64_t s_fun_id = 0;  // 使用static定义，生成唯一id
        RWMutexType::WriteLock lock(m_mutex);
        ++s_fun_id;
        m_cbs[s_fun_id] = cb;
        return s_fun_id;
    }

    // 删除回调函数, key 回调函数的唯一id
    void delListener(uint64_t key) {
        RWMutexType::WriteLock lock(m_mutex);
        m_cbs.erase(key);
    }

    // 获取回调函数, key 回调函数的唯一id, 如果存在返回对应的回调函数,否则返回nullptr
    on_change_cb getListener(uint64_t key) {
        RWMutexType::ReadLock lock(m_mutex);
        auto it = m_cbs.find(key);
        return it == m_cbs.end() ? nullptr : it->second;
    }

    // 清理所有的回调函数
    void clearListener() {
        RWMutexType::WriteLock lock(m_mutex);
        m_cbs.clear();
    }
private:
    RWMutexType m_mutex;
    T m_val;
    //变更回调函数组, uint64_t key,要求唯一，一般可以用hash
    std::map<uint64_t, on_change_cb> m_cbs;
};

// ConfigVar的管理类, 提供便捷的方法创建/访问ConfigVar
class Config {
public:
    typedef std::unordered_map<std::string, ConfigVarBase::ptr> ConfigVarMap;
    typedef RWMutex RWMutexType;

    /**
     * @brief 获取/创建对应参数名的配置参数
     * @param[in] name 配置参数名称
     * @param[in] default_value 参数默认值
     * @param[in] description 参数描述
     * @details 获取参数名为name的配置参数,如果存在直接返回
     *          如果不存在,创建参数配置并用default_value赋值
     * @return 返回对应的配置参数,如果参数名存在但是类型不匹配则返回nullptr
     * @exception 如果参数名包含非法字符[^0-9a-z_.] 抛出异常 std::invalid_argument
     */
    // 获取/创建对应参数名的配置参数
    template<class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name,
            const T& default_value, const std::string& description = "") {
        RWMutexType::WriteLock lock(GetMutex());
        auto it = GetDatas().find(name);
        // 如果有对应的配置项
        if(it != GetDatas().end()) {
            auto tmp = std::dynamic_pointer_cast<ConfigVar<T> >(it->second);  // 将基类指针转换为派生类指针, 即ConfigVar<T>的指针
            if(tmp) {
                SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "Lookup name=" << name << " exists";
                return tmp;
            } else {
                SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Lookup name=" << name << " exists but type not "
                        << TypeToName<T>() << " real_type=" << it->second->getTypeName()
                        << " " << it->second->toString();
                return nullptr;
            }
        }
        // 如果配置项的名称不符要求
        if(name.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678")
                != std::string::npos) {
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Lookup name invalid " << name;
            throw std::invalid_argument(name);
        }
        // 创建配置项, 使用typename明确告诉编译器，这是一个类模板中的类型, ptr是ConfigVar的shared_ptr
        typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, default_value, description));  // 使用智能指针的初始化
        GetDatas()[name] = v;  
        return v;
    }

    // 查找配置参数, name 配置参数名称, 返回配置参数名为name的配置参数 
    template<class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name) {
        RWMutexType::ReadLock lock(GetMutex());
        auto it = GetDatas().find(name);
        if(it == GetDatas().end()) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<ConfigVar<T> >(it->second);
    }

    // 使用YAML::Node初始化配置模块 
    static void LoadFromYaml(const YAML::Node& root);

    // 加载path文件夹里面的配置文件
    static void LoadFromConfDir(const std::string& path, bool force = false);

    // 查找配置参数,返回配置参数的基类, name 配置参数名称
    static ConfigVarBase::ptr LookupBase(const std::string& name);

    // 遍历配置模块里面所有配置项, cb 配置项回调函数
    static void Visit(std::function<void(ConfigVarBase::ptr)> cb);
private:

    // 返回所有的配置项, map, 
    static ConfigVarMap& GetDatas() {
        static ConfigVarMap s_datas;
        return s_datas;
    }

    // 配置项的RWMutex
    static RWMutexType& GetMutex() {
        static RWMutexType s_mutex;
        return s_mutex;
    }
};

}

#endif
