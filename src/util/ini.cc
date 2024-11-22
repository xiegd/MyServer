#include "ini.h"

#include "utility.h"

namespace xkernel {

using mIni = IniBasic<std::string, variant>;

mIni& IniBasic<std::string, variant>::Instance() {
    static mIni instance;
    return instance;
}


template <>
bool variant::as<bool>() const {
    if (empty() || isdigit(front())) {
        return as_default<bool>();
    }
    if (StringUtil::strToLower(std::string(*this)) == "true") {
        return true;
    }
    if (StringUtil::strToLower(std::string(*this)) == "false") {
        return false;
    }
    return as_default<bool>();
}

template <>
uint8_t variant::as<uint8_t>() const {
    return 0xFF & as_default<int>();
}

}