#include <gtest/gtest.h>
#include "file.h"
#include <fstream>
#include <cstdio>
#include <iostream>

class FileUtilTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建测试用的临时目录和文件
        system("mkdir -p test_dir");
        std::ofstream("test_dir/test_file.txt") << "test content";
    }

    void TearDown() override {
        // 清理测试用的临时目录和文件
        system("rm -rf test_dir");
    }
};

TEST_F(FileUtilTest, createPath) {
    EXPECT_TRUE(fileUtil::createPath("test_dir/new_dir/dir", 0755));
    EXPECT_TRUE(fileUtil::isDir("test_dir/new_dir/dir"));
}

TEST_F(FileUtilTest, createFile) {
    EXPECT_TRUE(fileUtil::createFile("test_dir/file.cc", "w"));
    EXPECT_TRUE(fileUtil::fileExist("test_dir/file.cc"));
}

TEST_F(FileUtilTest, isDir) {
    EXPECT_TRUE(fileUtil::isDir("test_dir"));
    EXPECT_FALSE(fileUtil::isDir("test_dir/test_file.txt"));
}

TEST_F(FileUtilTest, isSpecialDir) {
    EXPECT_TRUE(fileUtil::isSpecialDir("."));
    EXPECT_TRUE(fileUtil::isSpecialDir(".."));
    EXPECT_FALSE(fileUtil::isSpecialDir("test_dir"));
}

TEST_F(FileUtilTest, deleteFile) {
    fileUtil::createFile("test_dir/testDelete.txt", "w");
    EXPECT_EQ(fileUtil::deleteFile("test_dir/testDelete.txt"), 0);
    EXPECT_EQ(fopen("test_dir/testDelete.txt", "r"), nullptr);
}

TEST_F(FileUtilTest, fileExist) {
    EXPECT_TRUE(fileUtil::fileExist("test_dir/test_file.txt"));
    EXPECT_FALSE(fileUtil::fileExist("non_existent_file.txt"));
}

TEST_F(FileUtilTest, loadFile) {
    std::string content = fileUtil::loadFile("test_dir/test_file.txt");
    EXPECT_EQ(content, "test content");
}

TEST_F(FileUtilTest, saveFile) {
    fileUtil::createFile("test_dir/new_file.txt", "w");
    EXPECT_TRUE(fileUtil::saveFile("new content", "test_dir/new_file.txt"));
    std::string content = fileUtil::loadFile("test_dir/new_file.txt");
    EXPECT_EQ(content, "new content");
}

TEST_F(FileUtilTest, parentDir) {
    EXPECT_EQ(fileUtil::parentDir("test_dir/test_file.txt"), "test_dir/");
}

TEST_F(FileUtilTest, absolutePath) {
    EXPECT_EQ(fileUtil::absolutePath("./test_file.txt", ""), "/home/xie/MyServer/test/unittest/bin/test_file.txt");
}

TEST_F(FileUtilTest, scanDir) {

}

TEST_F(FileUtilTest, fileSize) {
    std::string s{"test content"};
    EXPECT_EQ(fileUtil::fileSize("test_dir/test_file.txt"), s.size());
    FILE* fp = fopen("test_dir/test_file.txt", "rb");
    fseek(fp, 0, SEEK_END);
    EXPECT_EQ(fileUtil::fileSize(fp, true), 0);
    fclose(fp);
}
 
class ExeFileTest : public ::testing::Test {};

TEST_F(ExeFileTest, exePath) {
    std::string path = exeFile::exePath(true);
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path, "./");
}

TEST_F(ExeFileTest, exeDir) {
    std::string dir = exeFile::exeDir(true);
    EXPECT_FALSE(dir.empty());
    EXPECT_EQ(dir, fileUtil::absolutePath("./", ""));
}

TEST_F(ExeFileTest, exeName) {
    std::string name = exeFile::exeName(true);
    EXPECT_FALSE(name.empty());
    EXPECT_EQ(name, "testFile");
}