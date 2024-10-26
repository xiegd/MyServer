#ifndef _FILE_H_
#define _FILE_H_

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <climits>

namespace xkernel {
class FileUtil {
public:
    static bool createPath(const std::string& file, unsigned int mod, bool is_dir = true);
    static FILE* createFile(const std::string& file, const std::string& mode = "w");
    static bool isDir(const std::string& path);
    static bool isSpecialDir(const std::string& path);  // 是否为特殊目录（. 或 ..）
    static int deleteFile(const std::string& path, bool del_empty_dir = false, bool backtrace = true);
    static bool fileExist(const std::string& path);
    static std::string loadFile(const std::string& path);  // 加载文件内容到string
    static bool saveFile(const std::string& data, const std::string& path);
    static std::string parentDir(const std::string& path);
    static std::string absolutePath(const std::string& path, const std::string& current_path, 
                                    bool can_access_parent = false);  // 相对路径转绝对路径
    static void scanDir(const std::string& path_in, 
                       const std::function<bool(const std::string& path, bool is_dir)>& cb, 
                       bool enter_sub_dir = false, bool show_hidden_file = false);  // 遍历目录
    static uint64_t fileSize(FILE* fp, bool remain_size = false);  // 获取文件大小
    static uint64_t fileSize(const std::string& path);  // 获取文件大小
    static void deleteEmptyDir(const std::string& dir, bool backtrace = true);  // 删除空目录

private:
    FileUtil() = delete;
    ~FileUtil() = delete;
};

class ExeFile {
public:
    static std::string exePath(bool isExe = true);
    static std::string exeDir(bool isExe = true);
    static std::string exeName(bool isExe = true);
private:
    ExeFile() = delete;
    ~ExeFile() = delete;
};
}
#endif // _FILE_H_