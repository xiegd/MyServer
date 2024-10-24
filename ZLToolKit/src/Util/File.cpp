/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>  // 目录操作
#include <limits.h>
#endif  // WIN32

#include <sys/stat.h>

#include "File.h"
#include "logger.h"
#include "util.h"
#include "uv_errno.h"

using namespace std;
using namespace toolkit;

#if !defined(_WIN32)
#define _unlink unlink
#define _rmdir rmdir
#define _access access
#endif

#if defined(_WIN32)

int mkdir(const char *path, int mode) { return _mkdir(path); }

DIR *opendir(const char *name) {
    char namebuf[512];
    snprintf(namebuf, sizeof(namebuf), "%s\\*.*", name);

    WIN32_FIND_DATAA FindData;
    auto hFind = FindFirstFileA(namebuf, &FindData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    DIR *dir = (DIR *)malloc(sizeof(DIR));
    memset(dir, 0, sizeof(DIR));
    dir->dd_fd = 0;  // simulate return
    dir->handle = hFind;
    return dir;
}

struct dirent *readdir(DIR *d) {
    HANDLE hFind = d->handle;
    WIN32_FIND_DATAA FileData;
    // fail or end
    if (!FindNextFileA(hFind, &FileData)) {
        return nullptr;
    }
    struct dirent *dir = (struct dirent *)malloc(sizeof(struct dirent) +
                                                 sizeof(FileData.cFileName));
    strcpy(dir->d_name, (char *)FileData.cFileName);
    dir->d_reclen = (uint16_t)strlen(dir->d_name);
    // check there is file or directory
    if (FileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        dir->d_type = 2;
    } else {
        dir->d_type = 1;
    }
    if (d->index) {
        //覆盖前释放内存  [AUTO-TRANSLATED:1cb478a1]
        // Release memory before covering
        free(d->index);
        d->index = nullptr;
    }
    d->index = dir;
    return dir;
}

int closedir(DIR *d) {
    if (!d) {
        return -1;
    }
    //关闭句柄  [AUTO-TRANSLATED:ec4f562d]
    // Close handle
    if (d->handle != INVALID_HANDLE_VALUE) {
        FindClose(d->handle);
        d->handle = INVALID_HANDLE_VALUE;
    }
    //释放内存  [AUTO-TRANSLATED:0f4046dc]
    // Release memory
    if (d->index) {
        free(d->index);
        d->index = nullptr;
    }
    free(d);
    return 0;
}
#endif  // defined(_WIN32)

namespace toolkit {

// 创建文件并返回文件指针
FILE *File::create_file(const std::string &file, const std::string &mode) {
    std::string path = file;
    std::string dir;
    size_t index = 1;
    FILE *ret = nullptr;
    while (true) {
        index = path.find('/', index) + 1;
        dir = path.substr(0, index);
        if (dir.length() == 0) {
            break;
        }
        if (_access(dir.data(), 0) == -1) {  // access函数是查看是不是存在
            if (mkdir(dir.data(), 0777) ==
                -1) {  //如果不存在就用mkdir函数来创建, 0777表示创建的目录权限(所有用户拥有全部的rwx)
                WarnL << "mkdir " << dir << " failed: " << get_uv_errmsg();
                return nullptr;
            }
        }
    }
    // 一般约定'/'结尾的路径表示目录，否则表示文件
    if (path[path.size() - 1] != '/') {
        ret = fopen(file.data(), mode.data());
    }
    return ret;
}

bool File::create_path(const std::string &file, unsigned int mod) {
    std::string path = file;
    std::string dir;
    size_t index = 1;
    // 逐步深入file路径，检查目录是否存在，不存在则创建
    while (true) {
        index = path.find('/', index) + 1;  // 跳过第一个'/'
        dir = path.substr(0, index);  // 获取当前要检查的目录
        if (dir.length() == 0) {
            break;
        }
        if (_access(dir.data(), 0) == -1) {  // access函数检查目录是否存在, 在当前的工作目录查找的
            if (mkdir(dir.data(), mod) == -1) {  //如果不存在就根据dir创建目录
                WarnL << "mkdir " << dir << " failed: " << get_uv_errmsg();
                return false;
            }
        }
    }
    return true;
}

//判断是否为目录
bool File::is_dir(const std::string &path) {
    auto dir = opendir(path.data());  // 打开目录, 打开失败返回null
    if (!dir) {
        return false;
    }
    closedir(dir);
    return true;
}

//判断是否为常规文件
bool File::fileExist(const std::string &path) {
    auto fp = fopen(path.data(), "rb");  // read binary
    if (!fp) {
        return false;
    }
    fclose(fp);
    return true;
}

//判断是否是特殊目录
bool File::is_special_dir(const std::string &path) {
    return path == "." || path == "..";
}

static int delete_file_l(const std::string &path_in) {
    DIR *dir;
    dirent *dir_info;
    auto path = path_in;
    if (path.back() == '/') {
        path.pop_back();
    }
    // 如果是目录则递归的删除目录中的文件
    if (File::is_dir(path)) {
        if ((dir = opendir(path.data())) == nullptr) {
            return _rmdir(path.data());
        }
        // 使用readdir遍历目录中的条目
        while ((dir_info = readdir(dir)) != nullptr) {
            // 忽略特殊目录, '.' 和 '..'
            if (File::is_special_dir(dir_info->d_name)) {
                continue;
            }
            File::delete_file(path + "/" + dir_info->d_name);
        }
        auto ret = _rmdir(path.data());
        closedir(dir);
        return ret;
    }
    // 不是目录则删除文件, 先尝试使用remove删除，删除失败再尝试unlink删除链接
    return remove(path.data()) ? _unlink(path.data()) : 0;
}

int File::delete_file(const std::string &path, bool del_empty_dir,
                      bool backtrace) {
    auto ret = delete_file_l(path);
    if (!ret && del_empty_dir) {
        // delete success
        File::deleteEmptyDir(parentDir(path), backtrace);
    }
    return ret;
}

string File::loadFile(const std::string &path) {
    FILE *fp = fopen(path.data(), "rb");
    if (!fp) {
        return "";
    }
    fseek(fp, 0, SEEK_END);  // 移动文件指针到末尾，末尾表示的是下一个要写入字节的位置
    auto len = ftell(fp);  // 获取文件指针当前位置相对于文件开头的偏移量， 对于SEEK_END即文件长度
    fseek(fp, 0, SEEK_SET);  // 移动文件指针到开头
    string str(len, '\0');
    if (len != (decltype(len))fread((char *)str.data(), 1, str.size(), fp)) {
        WarnL << "fread " << path << " failed: " << get_uv_errmsg();
    }
    fclose(fp);
    return str;
}

bool File::saveFile(const string &data, const std::string &path) {
    FILE *fp = fopen(path.data(), "wb");
    if (!fp) {
        return false;
    }
    fwrite(data.data(), data.size(), 1, fp);
    fclose(fp);
    return true;
}

string File::parentDir(const std::string &path) {
    auto parent_dir = path;
    if (parent_dir.back() == '/') {
        parent_dir.pop_back();
    }
    auto pos = parent_dir.rfind('/');
    // 如果没有找到则为npos
    if (pos != string::npos) {
        parent_dir = parent_dir.substr(0, pos + 1);
    }
    return parent_dir;
}

// 转换path为绝对路径, path这个相对路径时相对于current_path的, 也就是可执行文件的路径
string File::absolutePath(const std::string &path,
                          const std::string &current_path,
                          bool can_access_parent) {
    string currentPath = current_path;
    if (!currentPath.empty()) {
        //当前目录不为空 
        if (currentPath.front() == '.') {
            //如果当前目录是相对路径，那么先转换成绝对路径
            currentPath = absolutePath(current_path, exeDir(), true);
        }
    } else {
        currentPath = exeDir();
    }

    if (path.empty()) {
        //相对路径为空，那么返回当前目录 
        return currentPath;
    }

    if (currentPath.back() != '/') {
        //确保当前目录最后字节为'/' 
        currentPath.push_back('/');
    }
    auto rootPath = currentPath;
    auto dir_vec = split(path, "/");
    for (auto &dir : dir_vec) {
        if (dir.empty() || dir == ".") {
            //忽略空或本文件夹 
            continue;
        }
        if (dir == "..") {
            //访问上级目录 
            if (!can_access_parent && currentPath.size() <= rootPath.size()) {
                //不能访问根目录之外的目录, 返回根目录
                return rootPath;
            }
            currentPath = parentDir(currentPath);
            continue;
        }
        currentPath.append(dir);
        currentPath.append("/");
    }

    if (path.back() != '/' && currentPath.back() == '/') {
        //在路径是文件的情况下，防止转换成目录 
        currentPath.pop_back();
    }
    return currentPath;
}

// 扫描指定目录，对每个找到的文件/目录执行回调函数
void File::scanDir(const std::string &path_in,
                   const function<bool(const string &path, bool is_dir)> &cb,
                   bool enter_subdirectory, bool show_hidden_file) {
    string path = path_in;
    if (path.back() == '/') {
        path.pop_back();
    }

    DIR *pDir;
    dirent *pDirent;
    if ((pDir = opendir(path.data())) == nullptr) {
        //文件夹无效
        return;
    }
    while ((pDirent = readdir(pDir)) != nullptr) {
        if (is_special_dir(pDirent->d_name)) {
            continue;
        }
        if (!show_hidden_file && pDirent->d_name[0] == '.') {
            //隐藏的文件
            continue;
        }
        string strAbsolutePath = path + "/" + pDirent->d_name;
        bool isDir = is_dir(strAbsolutePath);
        if (!cb(strAbsolutePath, isDir)) {
            //不再继续扫描
            break;
        }

        if (isDir && enter_subdirectory) {
            //如果是文件夹并且扫描子文件夹，那么递归扫描
            scanDir(strAbsolutePath, cb, enter_subdirectory);
        }
    }
    closedir(pDir);
}

uint64_t File::fileSize(FILE *fp, bool remain_size) {
    if (!fp) {
        return 0;
    }
    auto current = ftell64(fp);
    fseek64(fp, 0L, SEEK_END); /* 定位到文件末尾 */
    auto end = ftell64(fp);    /* 得到文件大小 */
    fseek64(fp, current, SEEK_SET);  // 恢复文件指针位置
    return end - (remain_size ? current : 0);  // 返回文件大小/剩余文件大小
}

uint64_t File::fileSize(const std::string &path) {
    if (path.empty()) {
        return 0;
    }
    auto fp = std::unique_ptr<FILE, decltype(&fclose)>(fopen(path.data(), "rb"),
                                                       fclose);
    return fileSize(fp.get());
}

static bool isEmptyDir(const std::string &path) {
    bool empty = true;
    // 如果目录不为空，则会执行回调, 结束扫描，更新empty
    File::scanDir(
        path,
        [&](const std::string &path, bool isDir) {
            empty = false;
            return false;
        },
        true, true);
    return empty;
}

void File::deleteEmptyDir(const std::string &dir, bool backtrace) {
    if (!File::is_dir(dir) || !isEmptyDir(dir)) {
        // 不是文件夹或者非空
        return;
    }
    File::delete_file(dir);
    if (backtrace) {
        deleteEmptyDir(File::parentDir(dir), true);
    }
}
} /* namespace toolkit */
