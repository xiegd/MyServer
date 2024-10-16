#include <cstdio>
#include <iostream>

int main() {
    FILE* fp = fopen("./testBuffer.cc", "rb");
    if (!fp) {
        std::cerr << "open file failed" << std::endl;
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    auto len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    std::string str;
    for (int i = 10; i < len - 10; i++) {
        char c = fgetc(fp);
        str.push_back(c);
    }
    std::cout << "file size: " << len << std::endl;
    std::cout << str << std::endl;
    std::cout << "file size: " << str.size() << std::endl;
    fclose(fp);
    return 0;
}