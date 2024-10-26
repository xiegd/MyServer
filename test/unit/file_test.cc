#include <gtest/gtest.h>
#include "file.h"
#include "timeticker.h"
#include <fstream>
#include <cstdio>
#include <iostream>

using namespace xkernel;

class FileUtilTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建测试用的临时目录和文件
        system("mkdir -p test_dir");
        std::ofstream("test_dir/test_file.txt") << "test content";
    }

    void TearDown() override {
        // 清理测试用的临时目录和文件
        // system("rm -rf test_dir");
    }
};

TEST_F(FileUtilTest, createPath) {
    time_t second = time(nullptr);
    auto tm = TimeUtil::getLocalTime(second);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d-%02d-%02d_%02d.log", 1900 + tm.tm_year,
             1 + tm.tm_mon, tm.tm_mday, 11);
    std::cout << "buf: " << buf << ", " << strlen(buf) << std::endl;
    EXPECT_TRUE(FileUtil::createPath("test_dir/new_dir/dir.txt/", 0755, false));
    EXPECT_TRUE(FileUtil::isDir("test_dir/new_dir/dir"));
}

TEST_F(FileUtilTest, createFile) {
    EXPECT_TRUE(FileUtil::createFile("test_dir/file.cc", "w"));
    EXPECT_TRUE(FileUtil::fileExist("test_dir/file.cc"));
}

TEST_F(FileUtilTest, isDir) {
    EXPECT_TRUE(FileUtil::isDir("test_dir"));
    EXPECT_FALSE(FileUtil::isDir("test_dir/test_file.txt"));
}

TEST_F(FileUtilTest, isSpecialDir) {
    EXPECT_TRUE(FileUtil::isSpecialDir("."));
    EXPECT_TRUE(FileUtil::isSpecialDir(".."));
    EXPECT_FALSE(FileUtil::isSpecialDir("test_dir"));
}

TEST_F(FileUtilTest, deleteFile) {
    FileUtil::createFile("test_dir/testDelete.txt", "w");
    EXPECT_EQ(FileUtil::deleteFile("test_dir/testDelete.txt"), 0);
    EXPECT_EQ(fopen("test_dir/testDelete.txt", "r"), nullptr);
}

TEST_F(FileUtilTest, fileExist) {
    EXPECT_TRUE(FileUtil::fileExist("test_dir/test_file.txt"));
    EXPECT_FALSE(FileUtil::fileExist("non_existent_file.txt"));
}

TEST_F(FileUtilTest, loadFile) {
    std::string content = FileUtil::loadFile("test_dir/test_file.txt");
    EXPECT_EQ(content, "test content");
}

TEST_F(FileUtilTest, saveFile) {
    FileUtil::createFile("test_dir/new_file.txt", "w");
    EXPECT_TRUE(FileUtil::saveFile("new content", "test_dir/new_file.txt"));
    std::string content = FileUtil::loadFile("test_dir/new_file.txt");
    EXPECT_EQ(content, "new content");
}

TEST_F(FileUtilTest, parentDir) {
    EXPECT_EQ(FileUtil::parentDir("test_dir/test_file.txt"), "test_dir/");
}

TEST_F(FileUtilTest, absolutePath) {
    EXPECT_EQ(FileUtil::absolutePath("./test_file.txt", ""), "/home/xie/MyServer/test/unittest/bin/test_file.txt");
}

TEST_F(FileUtilTest, scanDir) {

}

TEST_F(FileUtilTest, fileSize) {
    std::string s{"test content"};
    EXPECT_EQ(FileUtil::fileSize("test_dir/test_file.txt"), s.size());
    FILE* fp = fopen("test_dir/test_file.txt", "rb");
    fseek(fp, 0, SEEK_END);
    EXPECT_EQ(FileUtil::fileSize(fp, true), 0);
    fclose(fp);
}
 
class ExeFileTest : public ::testing::Test {};

TEST_F(ExeFileTest, exePath) {
    std::string path = ExeFile::exePath(true);
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path, "./");
}

TEST_F(ExeFileTest, exeDir) {
    std::string dir = ExeFile::exeDir(true);
    EXPECT_FALSE(dir.empty());
    EXPECT_EQ(dir, FileUtil::absolutePath("./", ""));
}

TEST_F(ExeFileTest, exeName) {
    std::string name = ExeFile::exeName(true);
    EXPECT_FALSE(name.empty());
    EXPECT_EQ(name, "testFile");
}