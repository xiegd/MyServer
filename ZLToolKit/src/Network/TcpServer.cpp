/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "TcpServer.h"

#include "Util/onceToken.h"
#include "Util/uv_errno.h"

using namespace std;

namespace toolkit {

INSTANCE_IMP(SessionMap)
StatisticImp(TcpServer)

// 默认poller为空，表示创建主server, 否则创建cloned server
TcpServer::TcpServer(const EventPoller::Ptr &poller)
    : Server(poller) {
    _multi_poller = !poller;
    setOnCreateSocket(nullptr);
}

// 初始化_socket和接受连接前和接受时的回调，回调用于派发连接给poller和poller对应的cloned server
void TcpServer::setupEvent() {
    // 调用构造tcpserver时初始化的_on_create_socket回调函数创建socket
    _socket = createSocket(_poller);
    // shared_from_this()返回的是enable_shared_from_this基类的指针，需要转换为TcpServer类指针
    weak_ptr<TcpServer> weak_self =
        std::static_pointer_cast<TcpServer>(shared_from_this());  
    // 设置tcp收到accept请求后连接前的回调
    _socket->setOnBeforeAccept(
        [weak_self](const EventPoller::Ptr &poller) -> Socket::Ptr {
            // 检查tcpserver是否还有效，有效则调用onBeforeAcceptConnection
            // 选择poller和对应的cloned server负责后续的连接
            if (auto strong_self = weak_self.lock()) {
                return strong_self->onBeforeAcceptConnection(poller);
            }
            return nullptr;
        });
    // 设置tcp接收到连接时的回调
    _socket->setOnAccept(
        [weak_self](Socket::Ptr &sock, shared_ptr<void> &complete) {
            if (auto strong_self = weak_self.lock()) {
                auto ptr = sock->getPoller().get();
                auto server = strong_self->getServer(ptr);  // 获取poller对应的cloned server
                ptr->async([server, sock, complete]() {
                    // 该tcp客户端派发给对应线程(poller)的TcpServer服务器
                    server->onAcceptConnection(sock);
                });
            }
        });
}

TcpServer::~TcpServer() {
    if (_main_server && _socket && _socket->rawFD() != -1) {
        InfoL << "Close tcp server [" << _socket->get_local_ip()
              << "]: " << _socket->get_local_port();
    }
    _timer.reset();
    //先关闭socket监听，防止收到新的连接
    _socket.reset();
    _session_map.clear();
    _cloned_server.clear();
}

uint16_t TcpServer::getPort() {
    if (!_socket) {
        return 0;
    }
    return _socket->get_local_port();
}

// 设置创建socket的回调函数, 主server调用的cb为空
void TcpServer::setOnCreateSocket(Socket::onCreateSocket cb) {
    if (cb) {
        _on_create_socket = std::move(cb);
    } else {
        // 使用构造函数调用时 cb = nullptr, 设置_on_create_socket为创建默认socket
        _on_create_socket = [](const EventPoller::Ptr &poller) {
            return Socket::createSocket(poller, false);
        };
    }
    // 遍历cloned server, 设置cloned server的创建socket回调函数
    for (auto &pr : _cloned_server) {
        pr.second->setOnCreateSocket(cb);
    }
}

// 创建poller对应的cloned server
TcpServer::Ptr TcpServer::onCreatServer(const EventPoller::Ptr &poller) {
    return Ptr(new TcpServer(poller), [poller](TcpServer *ptr) {
        poller->async([ptr]() { delete ptr; });
    });
}

// 接受tcp连接前的处理逻辑
Socket::Ptr TcpServer::onBeforeAcceptConnection(
    const EventPoller::Ptr &poller) {
    assert(_poller->isCurrentThread());
    //此处改成自定义获取poller对象，防止负载不均衡
    // 如果是_multi_poller模式，则从EventPollerPool中获取poller, 用于后续的client连接
    return createSocket(
        _multi_poller ? EventPollerPool::Instance().getPoller(false) : _poller);
}

// 复制that的配置
void TcpServer::cloneFrom(const TcpServer &that) {
    if (!that._socket) {
        throw std::invalid_argument(
            "TcpServer::cloneFrom other with null socket");
    }
    setupEvent();
    _main_server = false;
    _on_create_socket = that._on_create_socket;
    _session_alloc = that._session_alloc;
    weak_ptr<TcpServer> weak_self =
        std::static_pointer_cast<TcpServer>(shared_from_this());
    _timer = std::make_shared<Timer>(
        2.0f,
        [weak_self]() -> bool {
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return false;
            }
            strong_self->onManagerSession();
            return true;
        },
        _poller);
    this->mINI::operator=(that);  // 复制配置
    _parent = static_pointer_cast<TcpServer>(
        const_cast<TcpServer &>(that).shared_from_this());
}

// 接收到客户端连接请求
Session::Ptr TcpServer::onAcceptConnection(const Socket::Ptr &sock) {
    assert(_poller->isCurrentThread());
    weak_ptr<TcpServer> weak_self =
        std::static_pointer_cast<TcpServer>(shared_from_this());
    //创建一个Session;这里实现创建不同的服务会话实例
    auto helper = _session_alloc(
        std::static_pointer_cast<TcpServer>(shared_from_this()), sock);
    auto session = helper->session();
    //把本服务器的配置传递给Session  
    session->attachServer(*this);

    //_session_map::emplace肯定能成功 , emplace方法返回一个pair<iterator, bool>
    // first为指向插入位置的迭代器，second是一个bool值，标识是否插入成功
    auto success = _session_map.emplace(helper.get(), helper).second;
    assert(success == true);

    weak_ptr<Session> weak_session = session;
    //会话接收数据事件  
    sock->setOnRead([weak_session](const Buffer::Ptr &buf, struct sockaddr *,
                                   int) {
        //获取会话强应用  
        auto strong_session = weak_session.lock();
        if (!strong_session) {
            return;
        }
        try {
            strong_session->onRecv(buf);
        } catch (SockException &ex) {
            strong_session->shutdown(ex);
        } catch (exception &ex) {
            strong_session->shutdown(SockException(Err_shutdown, ex.what()));
        }
    });

    SessionHelper *ptr = helper.get();
    auto cls = ptr->className();
    //会话接收到错误事件  
    sock->setOnErr(
        [weak_self, weak_session, ptr, cls](const SockException &err) {
            //在本函数作用域结束时移除会话对象  
            //目的是确保移除会话前执行其onError函数  
            //同时避免其onError函数抛异常时没有移除会话对象
            onceToken token(nullptr, [&]() {
                //移除掉会话  
                auto strong_self = weak_self.lock();
                if (!strong_self) {
                    return;
                }

                assert(strong_self->_poller->isCurrentThread());
                // 根据不同情况移除会话
                if (!strong_self->_is_on_manager) {
                    //该事件不是onManager时触发的，直接移除会话
                    strong_self->_session_map.erase(ptr);
                } else {
                    //遍历map时不能直接删除元素, 异步移除会话
                    strong_self->_poller->async(
                        [weak_self, ptr]() {
                            auto strong_self = weak_self.lock();
                            if (strong_self) {
                                strong_self->_session_map.erase(ptr);
                            }
                        },
                        false);
                }
            });

            //获取会话强应用  
            auto strong_session = weak_session.lock();
            if (strong_session) {
                //触发onError事件回调  
                TraceP(strong_session) << cls << " on err: " << err;
                strong_session->onError(err);
            }
        });
    return session;
}

void TcpServer::start_l(uint16_t port, const std::string &host,
                        uint32_t backlog) {
    setupEvent();

    //新建一个定时器定时管理这些tcp会话  
    weak_ptr<TcpServer> weak_self =
        std::static_pointer_cast<TcpServer>(shared_from_this());
    _timer = std::make_shared<Timer>(
        2.0f,
        // 定时器任务回调函数, true则重复执行, false则只执行一次
        [weak_self]() -> bool {
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return false;
            }
            strong_self->onManagerSession();
            return true;
        },
        _poller);

    if (_multi_poller) {
        // 遍历poller pool中的所有轮询器, 设置对应的server配置
        EventPollerPool::Instance().for_each(
            [&](const TaskExecutor::Ptr &executor) {
                EventPoller::Ptr poller =
                    static_pointer_cast<EventPoller>(executor);
                if (poller == _poller) {
                    return;  // 跳过主server
                }
                auto &serverRef = _cloned_server[poller.get()];
                if (!serverRef) {
                    serverRef = onCreatServer(poller);  // poller不存在对应的cloned server, 则创建
                }
                if (serverRef) {
                    serverRef->cloneFrom(*this);  // 存在，则复制主server的配置
                }
            });
    }

    if (!_socket->listen(port, host.c_str(), backlog)) {
        // 创建tcp监听失败，可能是由于端口占用或权限问题
        string err = (StrPrinter << "Listen on " << host << " " << port
                                 << " failed: " << get_uv_errmsg(true));
        throw std::runtime_error(err);
    }
    for (auto &pr : _cloned_server) {
        // 启动子Server  
        pr.second->_socket->cloneSocket(*_socket);
    }

    InfoL << "TCP server listening on [" << host << "]: " << port;
}

// 定期管理（检查）所有会话的状态
void TcpServer::onManagerSession() {
    assert(_poller->isCurrentThread());

    onceToken token([&]() { _is_on_manager = true; },
                    [&]() { _is_on_manager = false; });

    for (auto &pr : _session_map) {
        //遍历时，可能触发onErr事件(也会操作_session_map)
        try {
            pr.second->session()->onManager();
        } catch (exception &ex) {
            WarnL << ex.what();
        }
    }
}

// 使用设置的cb创建socket
Socket::Ptr TcpServer::createSocket(const EventPoller::Ptr &poller) {
    return _on_create_socket(poller);
}

// 获取poller对应的cloned server
TcpServer::Ptr TcpServer::getServer(const EventPoller *poller) const {
    auto parent = _parent.lock();
    // 获取监听的主server中存储的poller和cloned server对应关系的哈希表
    auto &ref = parent ? parent->_cloned_server : _cloned_server;
    auto it = ref.find(poller);
    if (it != ref.end()) {
        // 找到了，则派发到cloned server 
        return it->second;
    }
    // 没找到，则派发到parent server 
    return static_pointer_cast<TcpServer>(
        parent ? parent : const_cast<TcpServer *>(this)->shared_from_this());
}

Session::Ptr TcpServer::createSession(const Socket::Ptr &sock) {
    return getServer(sock->getPoller().get())->onAcceptConnection(sock);
}

} /* namespace toolkit */
