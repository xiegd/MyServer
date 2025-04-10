﻿/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "TcpClient.h"

using namespace std;

namespace toolkit {

StatisticImp(TcpClient)

TcpClient::TcpClient(const EventPoller::Ptr &poller)
    : SocketHelper(nullptr) {
    setPoller(poller ? poller : EventPollerPool::Instance().getPoller());
    // 设置创建socket时的回调函数
    setOnCreateSocket([](const EventPoller::Ptr &poller) {
        // TCP客户端默认开启互斥锁
        return Socket::createSocket(poller, true);
    });
}

TcpClient::~TcpClient() { TraceL << "~" << TcpClient::getIdentifier(); }

void TcpClient::shutdown(const SockException &ex) {
    _timer.reset();
    SocketHelper::shutdown(ex);
}

bool TcpClient::alive() const {
    if (_timer) {
        //连接中或已连接
        return true;
    }
    //在websocket client(zlmediakit)相关代码中, 
    //_timer一直为空，但是socket fd有效，alive状态也应该返回true
    auto sock = getSock();
    return sock && sock->alive();
}

void TcpClient::setNetAdapter(const string &local_ip) {
    _net_adapter = local_ip;
}

void TcpClient::startConnect(const string &url, uint16_t port,
                             float timeout_sec, uint16_t local_port) {
    weak_ptr<TcpClient> weak_self =
        static_pointer_cast<TcpClient>(shared_from_this());
        // 使用make_shared方法，自动使用默认的deleter，使用shared_ptr的构造函数，可以指定自定义deleter
    _timer = std::make_shared<Timer>(
        2.0f,
        [weak_self]() {
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return false;
            }
            strong_self->onManager();
            return true;
        },
        getPoller());

    setSock(createSocket());  // 调用父类SocketHelper的setSock方法，初始化sock_和poller_

    auto sock_ptr = getSock().get();  // 获取sock_指针， 避免循环引用
    sock_ptr->setOnErr([weak_self, sock_ptr](const SockException &ex) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        if (sock_ptr != strong_self->getSock().get()) {
            //已经重连socket，上次的socket的事件忽略掉
            return;
        }
        strong_self->_timer.reset();
        TraceL << strong_self->getIdentifier() << " on err: " << ex;
        strong_self->onError(ex);
    });

    TraceL << getIdentifier() << " start connect " << url << ":" << port;
    sock_ptr->connect(
        url, port,
        [weak_self](const SockException &err) {
            auto strong_self = weak_self.lock();
            if (strong_self) {
                strong_self->onSockConnect(err);
            }
        },
        timeout_sec, _net_adapter, local_port);
}

// 处理tcp client连接结果的回调
void TcpClient::onSockConnect(const SockException &ex) {
    TraceL << getIdentifier() << " connect result: " << ex;
    if (ex) {
        //连接失败
        _timer.reset();
        onConnect(ex);
        return;
    }

    auto sock_ptr = getSock().get();
    weak_ptr<TcpClient> weak_self =
        static_pointer_cast<TcpClient>(shared_from_this());
    sock_ptr->setOnFlush([weak_self, sock_ptr]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        if (sock_ptr != strong_self->getSock().get()) {
            //已经重连socket，上传socket的事件忽略掉
            return false;
        }
        strong_self->onFlush();
        return true;
    });

    sock_ptr->setOnRead(
        [weak_self, sock_ptr](const Buffer::Ptr &pBuf, struct sockaddr *, int) {
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return;
            }
            if (sock_ptr != strong_self->getSock().get()) {
                //已经重连socket，上传socket的事件忽略掉
                return;
            }
            try {
                strong_self->onRecv(pBuf);
            } catch (std::exception &ex) {
                strong_self->shutdown(SockException(Err_other, ex.what()));
            }
        });

    onConnect(ex);
}

std::string TcpClient::getIdentifier() const {
    if (_id.empty()) {
        static atomic<uint64_t> s_index{0};
        _id = toolkit::demangle(typeid(*this).name()) + "-" +
              to_string(++s_index);
    }
    return _id;
}

} /* namespace toolkit */
