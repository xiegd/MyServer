#include <atomic>
#include <memory>
#include <iostream>
#include <string>

int main() {
    std::string str = "1234567890";
    std::cout << "capacity: " << str.capacity() << ", size: " << str.size() << std::endl;
    std::cout << "address: " << static_cast<void*>(str.data()) << std::endl;
    str.erase(0, 5);
    std::cout << "capacity: " << str.capacity() << ", size: " << str.size() << std::endl;
    std::cout << "address: " << static_cast<void*>(str.data()) << std::endl;
    str.insert(0, "00000000");
    std::cout << "capacity: " << str.capacity() << ", size: " << str.size() << std::endl;
    return 0;
}