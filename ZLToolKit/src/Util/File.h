/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

/**
 * @file File.h
 * @brief 文件操作工具类
 * 
 * 本文件定义了一个名为 File 的工具类，提供了一系列静态方法用于文件和目录操作。
 * 这些操作包括创建路径、创建文件、判断目录、删除文件或目录、检查文件存在性、
 * 加载和保存文件内容、获取父目录、获取绝对路径、遍历目录、获取文件大小等功能。
 * 该类旨在简化文件系统相关的操作，提高代码的可读性和可维护性。
 */

#ifndef SRC_UTIL_FILE_H_
#define SRC_UTIL_FILE_H_

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

#include "util.h"

#if defined(__linux__)
#include <limits.h>
#endif

#if defined(_WIN32)
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif  // !PATH_MAX

struct dirent {
    long d_ino;              /* inode 编号 */
    off_t d_off;             /* 到下一个 dirent 的偏移量 */
    unsigned short d_reclen; /* d_name 的长度 */
    unsigned char d_type;    /* d_name 的类型 */
    char d_name[1];          /* 文件名（以 null 结尾）*/
};
typedef struct _dirdesc {
    int dd_fd;    /** 与目录关联的文件描述符 */
    long dd_loc;  /** 当前缓冲区中的偏移量 */
    long dd_size; /** getdirentries 返回的数据量 */
    char *dd_buf; /** 数据缓冲区 */
    int dd_len;   /** 数据缓冲区的大小 */
    long dd_seek; /** getdirentries 返回的魔术 cookie */
    HANDLE handle;
    struct dirent *index;
} DIR;
#define __dirfd(dp) ((dp)->dd_fd)

int mkdir(const char *path, int mode);
DIR *opendir(const char *);
int closedir(DIR *);
struct dirent *readdir(DIR *);

#endif  // defined(_WIN32)

#if defined(_WIN32) || defined(_WIN64)
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseek
#define ftell64 ftell
#endif

namespace toolkit {

/**
 * @brief 文件操作工具类
 * 
 * File 类提供了一系列静态方法，用于执行常见的文件和目录操作。
 * 这些方法包括创建路径、创建文件、判断目录类型、删除文件或目录、
 * 检查文件存在性、加载和保存文件内容、获取父目录、获取绝对路径、
 * 遍历目录、获取文件大小等功能。
 */
class File {
public:
    /**
     * @brief 创建路径
     * 
     * @param file 要创建的路径
     * @param mod 新创建目录的权限模式
     * @return 如果路径创建成功返回 true，否则返回 false
     */
    static bool create_path(const std::string &file, unsigned int mod);

    /**
     * @brief 创建新文件，如果需要会自动生成目录
     * 
     * @param file 要创建的文件路径
     * @param mode 文件打开模式
     * @return 如果文件创建成功，返回文件指针；否则返回 nullptr
     */
    static FILE *create_file(const std::string &file, const std::string &mode);

    /**
     * @brief 判断给定路径是否为目录
     * 
     * @param path 要判断的路径
     * @return 如果是目录返回 true，否则返回 false
     */
    static bool is_dir(const std::string &path);

    /**
     * @brief 判断是否为特殊目录（. 或 ..）
     * 
     * @param path 要判断的路径
     * @return 如果是特殊目录返回 true，否则返回 false
     */
    static bool is_special_dir(const std::string &path);

    /**
     * @brief 删除文件或目录
     * 
     * @param path 要删除的文件或目录的路径
     * @param del_empty_dir 是否删除空目录
     * @param backtrace 是否回溯删除上层空目录
     * @return 成功返回 0，失败返回错误码
     */
    static int delete_file(const std::string &path, bool del_empty_dir = false,
                           bool backtrace = true);

    /**
     * @brief 判断文件是否存在
     * 
     * @param path 要检查的文件路径
     * @return 如果文件存在返回 true，否则返回 false
     */
    static bool fileExist(const std::string &path);

    /**
     * @brief 加载文件内容到 string 中
     * 
     * @param path 要加载的文件路径
     * @return 文件内容的字符串
     */
    static std::string loadFile(const std::string &path);

    /**
     * @brief 保存内容到文件
     * 
     * @param data 要保存的内容
     * @param path 保存的文件路径
     * @return 保存成功返回 true，失败返回 false
     */
    static bool saveFile(const std::string &data, const std::string &path);

    /**
     * @brief 获取父文件夹路径
     * 
     * @param path 当前路径
     * @return 父文件夹路径
     */
    static std::string parentDir(const std::string &path);

    /**
     * @brief 替换路径中的 "../"，获取绝对路径
     * 
     * @param path 相对路径，可能包含 "../"
     * @param current_path 当前目录
     * @param can_access_parent 是否允许访问父目录之外的目录
     * @return 处理后的绝对路径
     */
    static std::string absolutePath(const std::string &path,
                                    const std::string &current_path,
                                    bool can_access_parent = false);

    /**
     * @brief 遍历文件夹下的所有文件
     * 
     * @param path 要遍历的文件夹路径
     * @param cb 回调函数，参数为文件的绝对路径和是否为目录的标志，返回 true 继续遍历，false 停止遍历
     * @param enter_subdirectory 是否进入子目录遍历
     * @param show_hidden_file 是否显示隐藏文件
     */
    static void scanDir(
        const std::string &path,
        const std::function<bool(const std::string &path, bool isDir)> &cb,
        bool enter_subdirectory = false, bool show_hidden_file = false);

    /**
     * @brief 获取文件大小
     * 
     * @param fp 文件句柄
     * @param remain_size 如果为 true，获取文件剩余未读数据大小；如果为 false，获取文件总大小
     * @return 文件大小（字节数）
     */
    static uint64_t fileSize(FILE *fp, bool remain_size = false);

    /**
     * @brief 获取文件大小
     * 
     * @param path 文件路径
     * @return 文件大小（字节数）
     * @warning 调用者应确保文件存在
     */
    static uint64_t fileSize(const std::string &path);

    /**
     * @brief 尝试删除空文件夹
     * 
     * @param dir 要删除的文件夹路径
     * @param backtrace 是否回溯删除上层空文件夹
     */
    static void deleteEmptyDir(const std::string &dir, bool backtrace = true);

private:
    File();  // 私有构造函数，防止实例化
    ~File(); // 私有析构函数
};

} /* namespace toolkit */
#endif /* SRC_UTIL_FILE_H_ */