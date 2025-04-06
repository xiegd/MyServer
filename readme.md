# MyServer - 高性能C++服务器框架

## 项目简介

MyServer是一个基于C++11开发的高性能服务器框架，参考了[zltoolkit](https://github.com/ZLMediaKit/ZLToolKit)的设计理念和实现方式。该框架提供了完整的TCP/UDP服务器实现，支持多线程、事件驱动等特性。

## 主要特性

- 基于事件驱动的多线程服务器架构
- 支持TCP和UDP协议
- 多级缓冲区设计，优化网络IO性能
- 支持多线程并发处理
- 灵活的事件处理机制
- 完善的Socket封装

## 项目结构

```
src/
├── network/     # 网络相关实现
│   ├── tcpserver.cc    # TCP服务器实现
│   ├── udpserver.cc    # UDP服务器实现
│   ├── socket.cc       # Socket封装
│   ├── buffer.cc       # 缓冲区实现
│   ├── sockutil.cc     # Socket工具类
│   └──buffersock.cc   # 封装Socket的发送和接收针对tcp和udp单独设计
├── poller/      # 事件轮询器
├── thread/      # 线程相关实现
└── util/        # 工具类
```

## 核心组件

### 1. 网络层
- `TcpServer`: TCP服务器实现
- `UdpServer`: UDP服务器实现
- `Socket`: 统一的Socket封装
- `Buffer`: 多级缓冲区设计

### 2. 事件处理
- `EventPoller`: 事件轮询器

### 3. 线程管理
- 线程池实现
- 任务调度

## 使用示例

```cpp
// 创建TCP服务器
TcpServer::Ptr server(new TcpServer());
server->start<SessionType>(8080);
```

## 构建说明

1. 依赖项：
   - C++11或更高版本
   - OpenSSL
   - CMake 3.0+

2. 构建步骤：
```bash
mkdir build
cd build
cmake ..
make
```

## 参考项目

本项目参考了[zltoolkit](https://github.com/ZLMediaKit/ZLToolKit)的设计和实现，特别感谢其作者的开源贡献。

## 许可证

MIT License

## 贡献

欢迎提交Issue和Pull Request来帮助改进这个项目。

## 联系方式

如有任何问题或建议，请通过Issue提交。