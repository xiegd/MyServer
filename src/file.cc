#include "file.h"

#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <string>
#include <memory>
#include <unistd.h>

#include "utility.h"


bool fileUtil::createPath(const std::string& file, unsigned int mod) {
    std::string path = file;
    std::string dir;
    size_t index = 1;
    if (path.back() != '/') {
        path.push_back('/');
    }
    while (true) {
        index = path.find('/', index) + 1;
        dir = path.substr(0, index);  // 获取当前要检查的目录
        if (dir.length() == 0) {
            break;
        }
        if (access(dir.data(), 0) == -1) {
            if (mkdir(dir.data(), mod) == -1) {
                // WarnL << "mkdir " << dir << " failed: " << get_uv_errmsg();
                return false;
            }
        }
    }
    return true;
}

FILE* fileUtil::createFile(const std::string& file, const std::string& mode) {
    std::string path = file;
    std::string dir;
    size_t index = 1;
    FILE* ret = nullptr;
    while (true) {
        index = path.find('/', index) + 1;
        dir = path.substr(0, index);
        if (dir.length() == 0) {
            break;
        }
        if (access(dir.data(), 0) == -1) {
            if (mkdir(dir.data(), 0777) == -1) {
                // WarnL << "mkdir " << dir << " failed: " << get_uv_errmsg();
                return nullptr;
            }
        }
    }
    if (path.back() != '/') {
        ret = fopen(file.data(), mode.data());
    }
    return ret;
}

bool fileUtil::isDir(const std::string& path) {
    auto dir = opendir(path.data());  // 打开目录, 打开失败返回null
    if (!dir) {
        return false;
    }
    closedir(dir);
    return true;
}

bool fileUtil::isSpecialDir(const std::string& path) {
    return path == "." || path == "..";
}

static int delete_file(const std::string& path_in) {
    DIR* dir;
    dirent* dir_info;
    auto path = path_in;  // 要删除的目录
    if (path.back() == '/') {
        path.pop_back();
    }
    if (fileUtil::isDir(path)) {
        // 打开失败则删除目录
        if ((dir = opendir(path.data())) == nullptr) {
            return rmdir(path.data());
        }
        // 使用readdir遍历目录中的条目, 
        while ((dir_info = readdir(dir)) != nullptr) {
            if (fileUtil::isSpecialDir(dir_info->d_name)) {
                continue;
            }
            fileUtil::deleteFile(path + "/" + dir_info->d_name);
        }
        auto ret = rmdir(path.data());
        closedir(dir);
        return ret;
    }
    return remove(path.data()) ? unlink(path.data()) : 0;
}

int fileUtil::deleteFile(const std::string& path, bool del_empty_dir, bool backtrace) {
    auto ret = delete_file(path);  // delete_file 进行实际的删除文件或目录操作
    if (!ret && del_empty_dir) {
        fileUtil::deleteEmptyDir(fileUtil::parentDir(path), backtrace);
    }
    return ret;
}

bool fileUtil::fileExist(const std::string& path) {
    auto fp = fopen(path.data(), "rb");
    if (!fp) {
        return false;
    }
    fclose(fp);
    return true;
}

std::string fileUtil::loadFile(const std::string& path) {
    FILE* fp = fopen(path.data(), "rb");
    if (!fp) {
        return "";
    }
    fseek(fp, 0, SEEK_END);
    auto len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    std::string str(len, '\0');
    if (len != (decltype(len))fread((char*)str.data(), 1, str.size(), fp)) {
        // WarnL << "fread " << path << " failed: " << get_uv_errmsg();
    }
    fclose(fp);
    return str;
}

bool fileUtil::saveFile(const std::string& data, const std::string& path) {
    FILE* fp = fopen(path.data(), "wb");  // 如果文件不存在则尝试创建
    if (!fp) {
        return false;
    }
    fwrite(data.data(), data.size(), 1, fp);
    fclose(fp);
    return true;
}

std::string fileUtil::parentDir(const std::string& path) {
    auto parent_dir = path;
    if (parent_dir.back() == '/') {
        parent_dir.pop_back();
    }
    auto pos = parent_dir.rfind('/');
    if (pos != std::string::npos) {
        parent_dir = parent_dir.substr(0, pos + 1);
    }
    return parent_dir;
}

// 转换path为绝对路径, path这个相对路径时相对于current_path的, 也就是可执行文件的路径
std::string fileUtil::absolutePath(const std::string& path, const std::string& current_path, bool can_access_parent) {
    std::string currentPath = current_path;
    // 处理当前路径
    if (!currentPath.empty()) {
        if (currentPath.front() == '.') {
            currentPath = absolutePath(current_path, exeFile::exeDir(), true);
        }
    }
    else {
        currentPath = exeFile::exeDir();
    }
    // 如果要转换的路径path为空， 则返回当前路径
    if (path.empty()) {
        return currentPath;
    }
    // 统一当前路径格式
    if (currentPath.back() != '/') {
        currentPath.push_back('/');
    }

    auto rootPath = currentPath;
    auto dir_vec = stringUtil::split(path, "/");
    // 转换path为绝对路径
    for (auto& dir : dir_vec) {
        if (dir.empty() || dir == ".") {
            continue;
        }
        if (dir == "..") {
            if (!can_access_parent && currentPath.size() <= rootPath.size()) {
                return rootPath;
            }
            currentPath = parentDir(currentPath);
            continue;
        }
        currentPath.append(dir);
        currentPath.append("/");
    }
    // 保持原始路径的格式
    if (path.back() != '/' && currentPath.back() == '/') {
        currentPath.pop_back();
    }
    return currentPath;
}

// 扫描指定目录，对每个找到的文件/目录执行回调函数
void fileUtil::scanDir(const std::string& path_in,
                        const std::function<bool(const std::string& path, bool is_dir)>& cb, 
                        bool enter_sub_dir, bool show_hidden_file) {
    std::string path = path_in;
    if (path.back() == '/') {
        path.pop_back();
    }

    DIR* pDir;
    dirent* pDirent;
    if ((pDir = opendir(path.data())) == nullptr) {
        return ;
    }
    while ((pDirent = readdir(pDir)) != nullptr) {
        if (isSpecialDir(pDirent->d_name)) {
            continue;
        }
        if (!show_hidden_file && pDirent->d_name[0] == '.') {
            continue;
        }
        std::string strAbsolutePath = path + "/" + pDirent->d_name;
        bool is_dir = isDir(strAbsolutePath);
        // 执行回调函数
        if (!cb(strAbsolutePath, is_dir)) {
            break;
        }
        if (is_dir && enter_sub_dir) {
            scanDir(strAbsolutePath, cb, enter_sub_dir);  // 递归扫描
        }
    }
    closedir(pDir);
}

uint64_t fileUtil::fileSize(FILE* fp, bool remain_size) {
    if (!fp) {
        return 0;
    }
    auto current = ftell(fp);
    fseek(fp, 0L, SEEK_END);
    auto end = ftell(fp);
    fseek(fp, current, SEEK_SET);
    return end - (remain_size ? current : 0);  // 返回文件大小/剩余文件大小
}

uint64_t fileUtil::fileSize(const std::string& path) {
    if (path.empty()) {
        return 0;
    }
    auto fp = std::unique_ptr<FILE, decltype(&fclose)>(fopen(path.data(), "rb"), fclose);
    return fileSize(fp.get());
}

static bool isEmptyDir(const std::string& path) {
    bool empty = true;
    // 如果目录不为空，则会执行回调, 结束扫描，更新empty
    fileUtil::scanDir(path, [&empty](const std::string& path, bool is_dir) {
        empty = false;
        return false;
    }, true, true);
    return empty;
}

void fileUtil::deleteEmptyDir(const std::string& dir, bool backtrace) {
    // 判断是否为空目录
    if (!isDir(dir) || !isEmptyDir(dir)) {
        return ;
    }
    deleteFile(dir);
    if (backtrace) {
        deleteEmptyDir(parentDir(dir), true);
    }
}

// 获取当前正在允许的可执行文件的路径
std::string exeFile::exePath(bool isExe) {
    char buffer[PATH_MAX * 2 + 1] = {0};
    int n = readlink("/proc/self/exe", buffer, sizeof(buffer));
    std::string filePath;
    filePath = n <= 0 ? "./" : buffer;
    return filePath;
}

// 获取当前正在允许的可执行文件的路径的目录
std::string exeFile::exeDir(bool isExe) {
    auto path = exePath(isExe);
    return path.substr(0, path.rfind('/') + 1);
}

// 获取当前正在允许的可执行文件的路径的文件名
std::string exeFile::exeName(bool isExe) {
    auto path = exePath(isExe);
    return path.substr(path.rfind('/') + 1);
}
