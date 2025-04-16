#include "TcpClient.h"

using namespace std;

namespace toolkit {

StatisticImp(TcpClient)

TcpClient::TcpClient(const EventPoller::Ptr &poller) : SocketHelper(nullptr) {
    setPoller(poller ? poller : EventPollerPool::Instance().getPoller());
    setOnCreateSocket([](const EventPoller::Ptr &poller) {
        //TCP client defaults to enabling mutex lock
        return Socket::createSocket(poller, true);
    });
}

TcpClient::~TcpClient() {
    TraceL << "~" << TcpClient::getIdentifier();
}

void TcpClient::shutdown(const SockException &ex) {
    _timer.reset();
    SocketHelper::shutdown(ex);
}

bool TcpClient::alive() const {
    if (_timer) {
        //Connecting or already connected
        return true;
    }
    //In websocket client (s3mediakit) related code,
    //_timer is always empty, but socket fd is valid, and alive status should also return true
    auto sock = getSock();
    return sock && sock->alive();
}

void TcpClient::setNetAdapter(const string &local_ip) {
    _net_adapter = local_ip;
}

void TcpClient::startConnect(const string &url, uint16_t port, float timeout_sec, uint16_t local_port) {
    weak_ptr<TcpClient> weak_self = static_pointer_cast<TcpClient>(shared_from_this());
    _timer = std::make_shared<Timer>(2.0f, [weak_self]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        strong_self->onManager();
        return true;
    }, getPoller());

    setSock(createSocket());

    auto sock_ptr = getSock().get();
    sock_ptr->setOnErr([weak_self, sock_ptr](const SockException &ex) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        if (sock_ptr != strong_self->getSock().get()) {
            //Socket has been reconnected, last socket's event is ignored
            return;
        }
        strong_self->_timer.reset();
        TraceL << strong_self->getIdentifier() << " on err: " << ex;
        strong_self->onError(ex);
    });

    TraceL << getIdentifier() << " start connect " << url << ":" << port;
    sock_ptr->connect(url, port, [weak_self](const SockException &err) {
        auto strong_self = weak_self.lock();
        if (strong_self) {
            strong_self->onSockConnect(err);
        }
    }, timeout_sec, _net_adapter, local_port);
}

void TcpClient::onSockConnect(const SockException &ex) {
    TraceL << getIdentifier() << " connect result: " << ex;
    if (ex) {
        //Connection failed
        _timer.reset();
        onConnect(ex);
        return;
    }

    auto sock_ptr = getSock().get();
    weak_ptr<TcpClient> weak_self = static_pointer_cast<TcpClient>(shared_from_this());
    sock_ptr->setOnFlush([weak_self, sock_ptr]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        if (sock_ptr != strong_self->getSock().get()) {
            //Socket has been reconnected, upload socket's event is ignored
            return false;
        }
        strong_self->onFlush();
        return true;
    });

    sock_ptr->setOnRead([weak_self, sock_ptr](const Buffer::Ptr &pBuf, struct sockaddr *, int) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        if (sock_ptr != strong_self->getSock().get()) {
            //Socket has been reconnected, upload socket's event is ignored
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
        static atomic<uint64_t> s_index { 0 };
        _id = toolkit::demangle(typeid(*this).name()) + "-" + to_string(++s_index);
    }
    return _id;
}

} /* namespace toolkit */
