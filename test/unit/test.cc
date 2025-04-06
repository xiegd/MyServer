#include <csignal>
#include <iostream>

#include "session.h"
#include "tcpserver.h"
#include "cmd_parser.h"
#include "logger.h"
#include "utility.h"

using namespace std;
using namespace xkernel;

// 回显会话
class EchoSession : public Session {
   public:
    EchoSession(const Socket::Ptr &pSock) : Session(pSock) { DebugL; }
    virtual ~EchoSession() { DebugL; }

    void onRecv(const Buffer::Ptr &buffer) override { send(buffer); }
    void onErr(const SockException &err) override { WarnL << err.what(); }
    void onFlush() override {
        DebugL << "onFlush";
    }
    void onManager() override {}
};

//命令(http)
class CMD_pingpong : public Cmd {
   public:
    CMD_pingpong() {
        parser_.reset(new OptionParser(nullptr));
        (*parser_) << Option('l', "listen", Option::ArgType::Required, "10000", false,
                             "服务器模式：监听端口", nullptr);
        //测试客户端个数，默认10个
        (*parser_) << Option('c', "count", Option::ArgType::Required,
                             to_string(10).data(), false,
                             "客户端模式：测试客户端个数", nullptr);
        //默认每次发送1MB的数据
        (*parser_) << Option('b', "block", Option::ArgType::Required,
                             to_string(1024 * 1024).data(), false,
                             "客户端模式：测试数据块大小", nullptr);
        //默认1秒发送10次，总速度率为1MB/s * 10 * 10 = 100MB/s
        (*parser_) << Option('i', "interval", Option::ArgType::Required,
                             to_string(100).data(), false,
                             "客户端模式：测试数据发送间隔，单位毫秒", nullptr);
        //客户端启动间隔时间
        (*parser_) << Option('d', "delay", Option::ArgType::Required, "50", false,
                             "服务器模式：客户端启动间隔时间", nullptr);

        //指定服务器地址
        (*parser_) << Option(
            's', "server", Option::ArgType::Required, "127.0.0.1:10000", false,
            "客户端模式：测试服务器地址",
            [](const std::shared_ptr<ostream> &stream, const string &arg) {
                if (arg.find(":") == string::npos) {
                    //中断后续选项的解析以及解析完毕回调等操作
                    throw std::runtime_error("\t地址必须指明端口号.");
                }
                //如果返回false则忽略后续选项的解析
                return true;
            });
    }

    ~CMD_pingpong() {}

    const char *description() const override { return "tcp回显性能测试"; }
};

EventPoller::Ptr nextPoller() {
    static vector<EventPoller::Ptr> s_poller_vec;
    static int s_poller_index = 0;
    if (s_poller_vec.empty()) {
        EventPollerPool::Instance().forEach(
            [&](const TaskExecutor::Ptr &executor) {
                s_poller_vec.emplace_back(
                    static_pointer_cast<EventPoller>(executor));
            });
    }
    auto ret = s_poller_vec[s_poller_index++];
    if (s_poller_index == s_poller_vec.size()) {
        s_poller_index = 0;
    }
    return ret;
}

int main(int argc, char *argv[]) {
    CMD_pingpong cmd;
    try {
        cmd(argc, argv);
    } catch (std::exception &ex) {
        cout << ex.what() << endl;
        return 0;
    }

    //初始化环境
    Logger::Instance().add(
        std::shared_ptr<ConsoleChannel>(new ConsoleChannel()));
    Logger::Instance().setWriter(
        std::shared_ptr<LogWriter>(new AsyncLogWriter()));

    {
        int interval = cmd["interval"];
        int block = cmd["block"];
        auto ip = cmd.splitedVal("server")[0];
        int port = cmd.splitedVal("server")[1];
        int delay = cmd["delay"];
        auto buffer = BufferRaw::create();
        buffer->setCapacity(block);
        buffer->setSize(block);

        TcpServer::Ptr server(new TcpServer);
        server->start<EchoSession>(cmd["listen"]);
        for (auto i = 0; i < cmd["count"].as<int>(); ++i) {
            auto poller = nextPoller();
            auto socket = Socket::createSocket(poller, false);

            socket->connect(
                ip, port,
                [socket, poller, interval, buffer](const SockException &err) {
                    if (err) {
                        WarnL << err.what();
                        return;
                    }
                    socket->setOnErr(
                        [](const SockException &err) { WarnL << err.what(); });
                    socket->setOnRead([interval, socket](
                                          const Buffer::Ptr &buffer,
                                          struct sockaddr *addr, int addr_len) {
                        if (!interval) {
                            socket->send(buffer);
                        }
                    });

                    if (interval) {
                        poller->doDelayTask(interval,
                                            [socket, interval, buffer]() {
                                                socket->send(buffer);
                                                return interval;
                                            });
                    } else {
                        socket->send(buffer);
                    }
                });
            usleep(delay * 2000);
        }

        //设置退出信号处理函数
        static semaphore sem;
        signal(SIGINT, [](int) { sem.post(); });  // 设置退出信号
        sem.wait();
    }
    return 0;
}