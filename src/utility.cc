#include "utility.h"

#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

std::string& stringUtil::strToLower(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

std::string stringUtil::strToLower(std::string&& str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return std::move(str);
}

std::string& stringUtil::strToUpper(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}

std::string stringUtil::strToUpper(std::string&& str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return std::move(str);
}

std::vector<std::string> stringUtil::split(const std::string& s, const char* delim) {
    std::vector<std::string> ret;
    size_t last = 0;
    auto index = s.find(delim, last);
    while (index != std::string::npos) {
        if (index - last > 0) {
            ret.push_back(s.substr(last, index - last));
        }
        last = index + strlen(delim);
        index = s.find(delim, last);
    }
    if (!s.size() || s.size() - last > 0) {
        ret.push_back(s.substr(last));
    }
    return ret;
}