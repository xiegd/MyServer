#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <iomanip>

#include <memory>
#include <iostream>

class BadWorker {
public:
    std::shared_ptr<BadWorker> createPtr() {
        auto ptr = std::shared_ptr<BadWorker>(this);
        return ptr;  // 返回而不是创建局部变量
    }
    ~BadWorker() {
        std::cout << "BadWorker被销毁" << std::endl;
    }
};

class GoodWorker : public std::enable_shared_from_this<GoodWorker> {
public:
    void createPtr() {
        auto ptr = shared_from_this();
        std::cout << "引用计数: " << ptr.use_count() << std::endl;  // 输出 2
    }
    ~GoodWorker() {
        std::cout << "GoodWorker被销毁" << std::endl;
    }
};

int main() {
    // 错误示例
    {
        auto worker = std::make_shared<BadWorker>();
        auto another = worker->createPtr();  // 保存返回的ptr
        // 现在会立即看到 double free 错误
    }

    std::cout << "--------------------------------" << std::endl;

    // 正确示例
    {
        auto worker = std::make_shared<GoodWorker>();
        std::cout << "初始引用计数: " << worker.use_count() << std::endl;  // 输出 1
        worker->createPtr();  // 使用现有的控制块
        // 程序正常结束
    }
}
