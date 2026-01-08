#include <type_traits>
#include "sockutil.h"
#include "Socket.h"
#include "Util/util.h"
#include "Util/logger.h"
#include "Util/uv_errno.h"
#include "Thread/semaphore.h"
#include "Poller/EventPoller.h"
#include "Thread/WorkThreadPool.h"
using namespace std;

#define LOCK_GUARD(mtx) lock_guard<decltype(mtx)> lck(mtx)

namespace toolkit {

StatisticImp(Socket)

static SockException toSockException(int error) {
    switch (error) {
        case 0:
        case UV_EAGAIN: return SockException(Err_success, "success");
        case UV_ECONNREFUSED: return SockException(Err_refused, uv_strerror(error), error);
        case UV_ETIMEDOUT: return SockException(Err_timeout, uv_strerror(error), error);
        case UV_ECONNRESET: return SockException(Err_reset, uv_strerror(error), error);
        default: return SockException(Err_other, uv_strerror(error), error);
    }
}

static SockException getSockErr(int sock, bool try_errno = true) {
    int error = 0, len = sizeof(int);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&error, (socklen_t *)&len);
    if (error == 0) {
        if (try_errno) {
            error = get_uv_error(true);
        }
    } else {
        error = uv_translate_posix_error(error);
    }
    return toSockException(error);
}

Socket::Ptr Socket::createSocket(const EventPoller::Ptr &poller_in, bool enable_mutex) {
    auto poller = poller_in ? poller_in : EventPollerPool::Instance().getPoller();
    std::weak_ptr<EventPoller> weak_poller = poller;
    return Socket::Ptr(new Socket(poller, enable_mutex), [weak_poller](Socket *ptr) {
        if (auto poller = weak_poller.lock()) {
            poller->async([ptr]() { delete ptr; });
        } else {
            delete ptr;
        }
    });
}

Socket::Socket(EventPoller::Ptr poller, bool enable_mutex)
    : _poller(std::move(poller))
    , _mtx_sock_fd(enable_mutex)
    , _mtx_event(enable_mutex)
    , _mtx_send_buf_waiting(enable_mutex)
    , _mtx_send_buf_sending(enable_mutex) {
    memset(&_peer_addr, 0, sizeof _peer_addr);
    setOnRead(nullptr);
    setOnErr(nullptr);
    setOnAccept(nullptr);
    setOnFlush(nullptr);
    setOnBeforeAccept(nullptr);
    setOnSendResult(nullptr);
}

Socket::~Socket() {
    closeSock();
}

void Socket::setOnRead(onReadCB cb) {
    onMultiReadCB cb2;
    if (cb) {
        cb2 = [cb](Buffer::Ptr *buf, struct sockaddr_storage *addr, size_t count) {
            for (auto i = 0u; i < count; ++i) {
                cb(buf[i], (struct sockaddr *)(addr + i), sizeof(struct sockaddr_storage));
            }
        };
    }
    setOnMultiRead(std::move(cb2));
}

void Socket::setOnMultiRead(onMultiReadCB cb) {
    LOCK_GUARD(_mtx_event);
    if (cb) {
        _on_multi_read = std::move(cb);
    } else {
        _on_multi_read = [](Buffer::Ptr *buf, struct sockaddr_storage *addr, size_t count) {
            for (auto i = 0u; i < count; ++i) {
                WarnL << "Socket not set read callback, data ignored: " << buf[i]->size();
            }
        };
    }
}

void Socket::setOnErr(onErrCB cb) {
    LOCK_GUARD(_mtx_event);
    if (cb) {
        _on_err = std::move(cb);
    } else {
        _on_err = [](const SockException &err) { WarnL << "Socket not set err callback, err: " << err; };
    }
}

void Socket::setOnAccept(onAcceptCB cb) {
    LOCK_GUARD(_mtx_event);
    if (cb) {
        _on_accept = std::move(cb);
    } else {
        _on_accept = [](Socket::Ptr &sock, shared_ptr<void> &complete) { WarnL << "Socket not set accept callback, peer fd: " << sock->rawFD(); };
    }
}

void Socket::setOnFlush(onFlush cb) {
    LOCK_GUARD(_mtx_event);
    if (cb) {
        _on_flush = std::move(cb);
    } else {
        _on_flush = []() { return true; };
    }
}

void Socket::setOnBeforeAccept(onCreateSocket cb) {
    LOCK_GUARD(_mtx_event);
    if (cb) {
        _on_before_accept = std::move(cb);
    } else {
        _on_before_accept = [](const EventPoller::Ptr &poller) { return nullptr; };
    }
}

void Socket::setOnSendResult(onSendResult cb) {
    LOCK_GUARD(_mtx_event);
    _send_result = std::move(cb);
}

void Socket::connect(const string &url, uint16_t port, const onErrCB &con_cb_in, float timeout_sec, const string &local_ip, uint16_t local_port) {
    weak_ptr<Socket> weak_self = shared_from_this();
    //Because it involves asynchronous callbacks, execute in the poller thread to ensure thread safety
    _poller->async([=] {
        if (auto strong_self = weak_self.lock()) {
            strong_self->connect_l(url, port, con_cb_in, timeout_sec, local_ip, local_port);
        }
    });
}

void Socket::connect_l(const string &url, uint16_t port, const onErrCB &con_cb_in, float timeout_sec, const string &local_ip, uint16_t local_port) {
    //Reset the current socket
    closeSock();

    weak_ptr<Socket> weak_self = shared_from_this();
    auto con_cb = [con_cb_in, weak_self](const SockException &err) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        strong_self->_async_con_cb = nullptr;
        strong_self->_con_timer = nullptr;
        if (err) {
            strong_self->setSock(nullptr);
        }
        con_cb_in(err);
    };

    auto async_con_cb = std::make_shared<function<void(const SockNum::Ptr &)>>([weak_self, con_cb](const SockNum::Ptr &sock) {
        auto strong_self = weak_self.lock();
        if (!sock || !strong_self) {
            con_cb(SockException(Err_dns, get_uv_errmsg(true)));
            return;
        }

        //Listen for whether the socket is writable, writable indicates that the connection to the server is successful
        int result = strong_self->_poller->addEvent(sock->rawFd(), EventPoller::Event_Write | EventPoller::Event_Error, [weak_self, sock, con_cb](int event) {
            if (auto strong_self = weak_self.lock()) {
                strong_self->onConnected(sock, con_cb);
            }
        });

        if (result == -1) {
            con_cb(SockException(Err_other, std::string("add event to poller failed when start connect:") + get_uv_errmsg()));
        } else {
            //First create the SockFD object to prevent SockNum from being destructed due to not executing delEvent
            strong_self->setSock(sock);
        }
    });

    //Connection timeout timer
    _con_timer = std::make_shared<Timer>(timeout_sec,[weak_self, con_cb]() {
        con_cb(SockException(Err_timeout, uv_strerror(UV_ETIMEDOUT)));
        return false;
    }, _poller);

    if (isIP(url.data())) {
        auto fd = SockUtil::connect(url.data(), port, true, local_ip.data(), local_port);
        (*async_con_cb)(fd == -1 ? nullptr : std::make_shared<SockNum>(fd, SockNum::Sock_TCP));
    } else {
        auto poller = _poller;
        weak_ptr<function<void(const SockNum::Ptr &)>> weak_task = async_con_cb;
        WorkThreadPool::Instance().getExecutor()->async([url, port, local_ip, local_port, weak_task, poller]() {
            //Blocking DNS resolution is executed in the background thread
            int fd = SockUtil::connect(url.data(), port, true, local_ip.data(), local_port);
            auto sock = fd == -1 ? nullptr : std::make_shared<SockNum>(fd, SockNum::Sock_TCP);
            poller->async([sock, weak_task]() {
                if (auto strong_task = weak_task.lock()) {
                    (*strong_task)(sock);
                }
            });
        });
        _async_con_cb = async_con_cb;
    }
}

void Socket::onConnected(const SockNum::Ptr &sock, const onErrCB &cb) {
    auto err = getSockErr(sock->rawFd(), false);
    if (err) {
        //Connection failed
        cb(err);
        return;
    }

    //Update address information
    setSock(sock);
    //First delete the previous writable event listener
    _poller->delEvent(sock->rawFd(), [sock](bool) {});
    if (!attachEvent(sock)) {
        //Connection failed
        cb(SockException(Err_other, "add event to poller failed when connected"));
        return;
    }

    {
        LOCK_GUARD(_mtx_sock_fd);
        if (_sock_fd) {
            _sock_fd->setConnected();
        }
    }
    //Connection successful
    cb(err);
}

bool Socket::attachEvent(const SockNum::Ptr &sock) {
    weak_ptr<Socket> weak_self = shared_from_this();
    if (sock->type() == SockNum::Sock_TCP_Server) {
        //TCP server
        auto result = _poller->addEvent(sock->rawFd(), EventPoller::Event_Read | EventPoller::Event_Error, [weak_self, sock](int event) {
            if (auto strong_self = weak_self.lock()) {
                strong_self->onAccept(sock, event);
            }
        });
        return -1 != result;
    }

    //TCP client or UDP
    auto read_buffer = _poller->getSharedBuffer(sock->type() == SockNum::Sock_UDP);
    auto result = _poller->addEvent(sock->rawFd(), EventPoller::Event_Read | EventPoller::Event_Error | EventPoller::Event_Write, [weak_self, sock, read_buffer](int event) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }

        if (event & EventPoller::Event_Read) {
            strong_self->onRead(sock, read_buffer);
        }
        if (event & EventPoller::Event_Write) {
            strong_self->onWriteAble(sock);
        }
        if (event & EventPoller::Event_Error) {
            if (sock->type() == SockNum::Sock_UDP) {
                // udp ignore error
            } else {
                strong_self->emitErr(getSockErr(sock->rawFd()));
            }
        }
    });

    return -1 != result;
}

ssize_t Socket::onRead(const SockNum::Ptr &sock, const SocketRecvBuffer::Ptr &buffer) noexcept {
    ssize_t ret = 0, nread = 0, count = 0;

    while (_enable_recv) {
        nread = buffer->recvFromSocket(sock->rawFd(), count);
        if (nread == 0) {
            if (sock->type() == SockNum::Sock_TCP) {
                emitErr(SockException(Err_eof, "end of file"));
            } else {
                WarnL << "Recv eof on udp socket[" << sock->rawFd() << "]";
            }
            return ret;
        }

        if (nread == -1) {
            auto err = get_uv_error(true);
            if (err != UV_EAGAIN) {
                if (sock->type() == SockNum::Sock_TCP) {
                    emitErr(toSockException(err));
                } else {
                    WarnL << "Recv err on udp socket[" << sock->rawFd() << "]: " << uv_strerror(err);
                }
            }
            return ret;
        }

        ret += nread;
        if (_enable_speed) {
            //Update receive rate
            _recv_speed += nread;
        }

        auto &buf = buffer->getBuffer(0);
        auto &addr = buffer->getAddress(0);
        try {
            //Catch exception here, the purpose is to prevent data from not being read completely, and the epoll edge trigger fails
            LOCK_GUARD(_mtx_event);
            _on_multi_read(&buf, &addr, count);
        } catch (std::exception &ex) {
            ErrorL << "Exception occurred when emit on_read: " << ex.what();
        }
    }
    return 0;
}

bool Socket::emitErr(const SockException &err) noexcept {
    if (_err_emit) {
        return true;
    }
    _err_emit = true;
    weak_ptr<Socket> weak_self = shared_from_this();
    _poller->async([weak_self, err]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        LOCK_GUARD(strong_self->_mtx_event);
        try {
            strong_self->_on_err(err);
        } catch (std::exception &ex) {
            ErrorL << "Exception occurred when emit on_err: " << ex.what();
        }
        //Delay closing the socket, only remove its IO event, to prevent Session object destruction from failing to get fd related information
        strong_self->closeSock(false);
    });
    return true;
}

ssize_t Socket::send(const char *buf, size_t size, struct sockaddr *addr, socklen_t addr_len, bool try_flush) {
    if (size <= 0) {
        size = strlen(buf);
        if (!size) {
            return 0;
        }
    }
    auto ptr = BufferRaw::create();
    ptr->assign(buf, size);
    return send(std::move(ptr), addr, addr_len, try_flush);
}

ssize_t Socket::send(string buf, struct sockaddr *addr, socklen_t addr_len, bool try_flush) {
    return send(std::make_shared<BufferString>(std::move(buf)), addr, addr_len, try_flush);
}

ssize_t Socket::send(Buffer::Ptr buf, struct sockaddr *addr, socklen_t addr_len, bool try_flush) {
    if (!addr) {
        if (!_udp_send_dst) {
            return send_l(std::move(buf), false, try_flush);
        }
        //This send did not specify a target address, but the target is customized through bindPeerAddr
        addr = (struct sockaddr *)_udp_send_dst.get();
        addr_len = SockUtil::get_sock_len(addr);
    } else {
        if (_peer_addr.ss_family != AF_UNSPEC) {
            // After udp connect, you cannot specify other addresses in sendto.
            return send_l(std::move(buf), false, try_flush);
        }
    }
    return send_l(std::make_shared<BufferSock>(std::move(buf), addr, addr_len), true, try_flush);
}

ssize_t Socket::send_l(Buffer::Ptr buf, bool is_buf_sock, bool try_flush) {
    auto size = buf ? buf->size() : 0;
    if (!size) {
        return 0;
    }

    {
        LOCK_GUARD(_mtx_send_buf_waiting);
        _send_buf_waiting.emplace_back(std::move(buf), is_buf_sock);
    }

    if (try_flush) {
        if (flushAll()) {
            return -1;
        }
    }

    return size;
}

int Socket::flushAll() {
    LOCK_GUARD(_mtx_sock_fd);

    if (!_sock_fd) {
        //If the connection is already disconnected or the send has timed out
        return -1;
    }
    if (_sendable) {
        //The socket is writable
        return flushData(_sock_fd->sockNum(), false) ? 0 : -1;
    }

    //The socket is not writable, judging send timeout
    if (_send_flush_ticker.elapsedTime() > _max_send_buffer_ms) {
        //If the oldest data in the send queue exceeds the timeout limit, disconnect the socket connection
        emitErr(SockException(Err_other, "socket send timeout"));
        return -1;
    }
    return 0;
}

void Socket::onFlushed() {
    bool flag;
    {
        LOCK_GUARD(_mtx_event);
        flag = _on_flush();
    }
    if (!flag) {
        setOnFlush(nullptr);
    }
}

void Socket::closeSock(bool close_fd) {
    _sendable = true;
    _enable_recv = true;
    _enable_speed = false;
    _con_timer = nullptr;
    _async_con_cb = nullptr;
    _send_flush_ticker.resetTime();

    {
        LOCK_GUARD(_mtx_send_buf_waiting);
        _send_buf_waiting.clear();
    }

    {
        LOCK_GUARD(_mtx_send_buf_sending);
        _send_buf_sending.clear();
    }

    {
        LOCK_GUARD(_mtx_sock_fd);
        if (close_fd) {
            _err_emit = false;
            _sock_fd = nullptr;
        } else if (_sock_fd) {
            _sock_fd->delEvent();
        }
    }
}

size_t Socket::getSendBufferCount() {
    size_t ret = 0;
    {
        LOCK_GUARD(_mtx_send_buf_waiting);
        ret += _send_buf_waiting.size();
    }

    {
        LOCK_GUARD(_mtx_send_buf_sending);
        _send_buf_sending.for_each([&](BufferList::Ptr &buf) { ret += buf->count(); });
    }
    return ret;
}

uint64_t Socket::elapsedTimeAfterFlushed() {
    return _send_flush_ticker.elapsedTime();
}

size_t Socket::getRecvSpeed() {
    _enable_speed = true;
    return _recv_speed.getSpeed();
}

size_t Socket::getSendSpeed() {
    _enable_speed = true;
    return _send_speed.getSpeed();
}

size_t Socket::getRecvTotalBytes() {
    _enable_speed = true;
    return _recv_speed.getTotalBytes();
}

size_t Socket::getSendTotalBytes() {
    _enable_speed = true;
    return _send_speed.getTotalBytes();
}

bool Socket::listen(uint16_t port, const string &local_ip, int backlog) {
    closeSock();
    int fd = SockUtil::listen(port, local_ip.data(), backlog);
    if (fd == -1) {
        return false;
    }
    return fromSock_l(std::make_shared<SockNum>(fd, SockNum::Sock_TCP_Server));
}

bool Socket::bindUdpSock(uint16_t port, const string &local_ip, bool enable_reuse) {
    closeSock();
    int fd = SockUtil::bindUdpSock(port, local_ip.data(), enable_reuse);
    if (fd == -1) {
        return false;
    }
    return fromSock_l(std::make_shared<SockNum>(fd, SockNum::Sock_UDP));
}

bool Socket::fromSock(int fd, SockNum::SockType type) {
    closeSock();
    SockUtil::setNoSigpipe(fd);
    SockUtil::setNoBlocked(fd);
    SockUtil::setCloExec(fd);
    return fromSock_l(std::make_shared<SockNum>(fd, type));
}

bool Socket::fromSock_l(SockNum::Ptr sock) {
    if (!attachEvent(sock)) {
        return false;
    }
    setSock(std::move(sock));
    return true;
}

void Socket::moveTo(EventPoller::Ptr poller) {
    LOCK_GUARD(_mtx_sock_fd);
    if (poller) {
        _poller = std::move(poller);
    }
    if (_sock_fd) {
        _sock_fd = std::make_shared<SockFD>(_sock_fd->sockNum(), _poller);
    }
}

int Socket::onAccept(const SockNum::Ptr &sock, int event) noexcept {
    int fd;
    struct sockaddr_storage peer_addr;
    socklen_t addr_len = sizeof(peer_addr);
    while (true) {
        if (event & EventPoller::Event_Read) {
            do {
                fd = (int)accept(sock->rawFd(), (struct sockaddr *)&peer_addr, &addr_len);
            } while (-1 == fd && UV_EINTR == get_uv_error(true));

            if (fd == -1) {
                //Accept failed
                int err = get_uv_error(true);
                if (err == UV_EAGAIN) {
                    //No new connection
                    return 0;
                }
                auto ex = toSockException(err);
                // emitErr(ex); https://github.com/S3MediaKit/S3MediaKit/issues/2946
                ErrorL << "Accept socket failed: " << ex.what();
                //Possibly too many open file descriptors: UV_EMFILE/UV_ENFILE
#if (defined(HAS_EPOLL) && !defined(_WIN32)) || defined(HAS_KQUEUE) 
                //Edge trigger, need to manually trigger the accept event again
                // wepoll, Edge-triggered (`EPOLLET`) mode isn't supported.
                std::weak_ptr<Socket> weak_self = shared_from_this();
                _poller->doDelayTask(100, [weak_self, sock]() {
                    if (auto strong_self = weak_self.lock()) {
                        //Process the accept event again after 100ms, maybe there are available fds
                        strong_self->onAccept(sock, EventPoller::Event_Read);
                    }
                    return 0;
                });
                //Temporarily do not process the accept event, wait 100ms and manually trigger onAccept (can only be triggered again through epoll after EAGAIN reads empty)
                return -1;
#else
                //Level trigger; sleep 10ms to prevent unnecessary accept failures
                this_thread::sleep_for(std::chrono::milliseconds(10));
                //Temporarily do not process the accept event, as it is level trigger, it will automatically enter the onAccept function again next time
                return -1;
#endif
            }

            SockUtil::setNoSigpipe(fd);
            SockUtil::setNoBlocked(fd);
            SockUtil::setNoDelay(fd);
            SockUtil::setSendBuf(fd);
            SockUtil::setRecvBuf(fd);
            SockUtil::setCloseWait(fd);
            SockUtil::setCloExec(fd);

            Socket::Ptr peer_sock;
            try {
                //Catch exceptions here to prevent the problem of epoll edge trigger failure when the socket is not fully accepted
                LOCK_GUARD(_mtx_event);
                //Intercept the Socket object's constructor
                peer_sock = _on_before_accept(_poller);
            } catch (std::exception &ex) {
                ErrorL << "Exception occurred when emit on_before_accept: " << ex.what();
                close(fd);
                continue;
            }

            if (!peer_sock) {
                //This is the default construction behavior, which means the child Socket shares the parent Socket's poll thread and closes the mutex lock
                peer_sock = Socket::createSocket(_poller, false);
            }

            auto sock = std::make_shared<SockNum>(fd, SockNum::Sock_TCP);
            //Set the fd properly, so that it can be accessed normally in the onAccept event
            peer_sock->setSock(sock);
            //Assign the peer ip to prevent the fd from being reset and disconnected when executing setSock
            memcpy(&peer_sock->_peer_addr, &peer_addr, addr_len);

            shared_ptr<void> completed(nullptr, [peer_sock, sock](void *) {
                try {
                    //Then add the fd to the poll monitoring (ensure that the onAccept event is triggered first, followed by onRead and other events)
                    if (!peer_sock->attachEvent(sock)) {
                        //If adding to poll monitoring fails, trigger the onErr event to notify that the Socket is invalid
                        peer_sock->emitErr(SockException(Err_eof, "add event to poller failed when accept a socket"));
                    }
                } catch (std::exception &ex) {
                    ErrorL << "Exception occurred: " << ex.what();
                }
            });

            try {
                //Catch exceptions here to prevent the problem of socket not being accepted and epoll edge triggering failure
                LOCK_GUARD(_mtx_event);
                //First trigger the onAccept event, at this point, you should listen for onRead and other events of the Socket
                _on_accept(peer_sock, completed);
            } catch (std::exception &ex) {
                ErrorL << "Exception occurred when emit on_accept: " << ex.what();
                continue;
            }
        }

        if (event & EventPoller::Event_Error) {
            auto ex = getSockErr(sock->rawFd());
            emitErr(ex);
            ErrorL << "TCP listener occurred a err: " << ex.what();
            return -1;
        }
    }
}

void Socket::setSock(SockNum::Ptr sock) {
    LOCK_GUARD(_mtx_sock_fd);
    if (sock) {
        _sock_fd = std::make_shared<SockFD>(std::move(sock), _poller);
        SockUtil::get_sock_local_addr(_sock_fd->rawFd(), _local_addr);
        SockUtil::get_sock_peer_addr(_sock_fd->rawFd(), _peer_addr);
    } else {
        _sock_fd = nullptr;
    }
}

string Socket::get_local_ip() {
    LOCK_GUARD(_mtx_sock_fd);
    if (!_sock_fd) {
        return "";
    }
    return SockUtil::inet_ntoa((struct sockaddr *)&_local_addr);
}

uint16_t Socket::get_local_port() {
    LOCK_GUARD(_mtx_sock_fd);
    if (!_sock_fd) {
        return 0;
    }
    return SockUtil::inet_port((struct sockaddr *)&_local_addr);
}

const sockaddr *Socket::get_local_addr() {
    return (const sockaddr*)&_local_addr;
}

const sockaddr *Socket::get_peer_addr() {
    if (_udp_send_dst)
        return (const sockaddr *)_udp_send_dst.get();
    else
        return (const sockaddr *)&_peer_addr;
}

string Socket::get_peer_ip() {
    LOCK_GUARD(_mtx_sock_fd);
    if (!_sock_fd) {
        return "";
    }
    if (_udp_send_dst) {
        return SockUtil::inet_ntoa((struct sockaddr *)_udp_send_dst.get());
    }
    return SockUtil::inet_ntoa((struct sockaddr *)&_peer_addr);
}

uint16_t Socket::get_peer_port() {
    LOCK_GUARD(_mtx_sock_fd);
    if (!_sock_fd) {
        return 0;
    }
    if (_udp_send_dst) {
        return SockUtil::inet_port((struct sockaddr *)_udp_send_dst.get());
    }
    return SockUtil::inet_port((struct sockaddr *)&_peer_addr);
}

string Socket::getIdentifier() const {
    static string class_name = "Socket: ";
    return class_name + to_string(reinterpret_cast<uint64_t>(this));
}

bool Socket::flushData(const SockNum::Ptr &sock, bool poller_thread) {
    decltype(_send_buf_sending) send_buf_sending_tmp;
    {
        //Transfer out of the secondary cache
        LOCK_GUARD(_mtx_send_buf_sending);
        if (!_send_buf_sending.empty()) {
            send_buf_sending_tmp.swap(_send_buf_sending);
        }
    }

    if (send_buf_sending_tmp.empty()) {
        _send_flush_ticker.resetTime();
        do {
            {
                //The secondary send cache is empty, so we continue to consume data from the primary cache
                LOCK_GUARD(_mtx_send_buf_waiting);
                if (!_send_buf_waiting.empty()) {
                    //Put the data from the first-level cache into the second-level cache and clear it
                    LOCK_GUARD(_mtx_event);
                    auto send_result = _enable_speed ? [this](const Buffer::Ptr &buffer, bool send_success) {
                        if (send_success) {
                            //Update the sending rate
                            _send_speed += buffer->size();
                        }
                        LOCK_GUARD(_mtx_event);
                        if (_send_result) {
                            _send_result(buffer, send_success);
                        }
                    } : _send_result;
                    send_buf_sending_tmp.emplace_back(BufferList::create(std::move(_send_buf_waiting), std::move(send_result), sock->type() == SockNum::Sock_UDP));
                    break;
                }
            }
            //If the first-level cache is also empty, it means that all data has been written to the socket
            if (poller_thread) {
                //The poller thread triggers this function, so the socket should have been added to the writable event listening
                //So, in the case of data queue clearing, we need to close the listening to avoid triggering meaningless event callbacks
                stopWriteAbleEvent(sock);
                onFlushed();
            }
            return true;
        } while (false);
    }

    while (!send_buf_sending_tmp.empty()) {
        auto &packet = send_buf_sending_tmp.front();
        auto n = packet->send(sock->rawFd(), _sock_flags);
        if (n > 0) {
            //All or part of the data was sent successfully
            if (packet->empty()) {
                //All data was sent successfully
                send_buf_sending_tmp.pop_front();
                continue;
            }
            //Part of the data was sent successfully
            if (!poller_thread) {
                //If this function is triggered by the poller thread, the socket should have been added to the writable event listening, so we don't need to add listening again
                startWriteAbleEvent(sock);
            }
            break;
        }

        //None of the data was sent successfully
        int err = get_uv_error(true);
        if (err == UV_EAGAIN) {
            //Wait for the next send
            if (!poller_thread) {
                //If this function is triggered by the poller thread, the socket should have already been added to the writable event listener, so we don't need to add it again
                startWriteAbleEvent(sock);
            }
            break;
        }

        //Other error codes, an exception occurred
        if (sock->type() == SockNum::Sock_UDP) {
            //UDP send exception, discard the data
            send_buf_sending_tmp.pop_front();
            WarnL << "Send udp socket[" << sock->rawFd() << "] failed, data ignored: " << uv_strerror(err);
            continue;
        }
        //TCP send failed, trigger an exception
        emitErr(toSockException(err));
        return false;
    }

    //Roll back the unsent data
    if (!send_buf_sending_tmp.empty()) {
        //There is remaining data
        LOCK_GUARD(_mtx_send_buf_sending);
        send_buf_sending_tmp.swap(_send_buf_sending);
        _send_buf_sending.append(send_buf_sending_tmp);
        //The secondary cache has not been sent completely, indicating that the socket is not writable, return directly
        return true;
    }

    //The secondary cache has been sent completely, indicating that the socket is still writable, we try to continue writing
    //If it's the poller thread, we try to write again (because other threads may have called the send function and there is new data)
    return poller_thread ? flushData(sock, poller_thread) : true;
}

void Socket::onWriteAble(const SockNum::Ptr &sock) {
    bool empty_waiting;
    bool empty_sending;
    {
        LOCK_GUARD(_mtx_send_buf_waiting);
        empty_waiting = _send_buf_waiting.empty();
    }

    {
        LOCK_GUARD(_mtx_send_buf_sending);
        empty_sending = _send_buf_sending.empty();
    }

    if (empty_waiting && empty_sending) {
        //Data has been cleared, we stop listening for writable events
        stopWriteAbleEvent(sock);
    } else {
        //Socket is writable, we try to send the remaining data
        flushData(sock, true);
    }
}

void Socket::startWriteAbleEvent(const SockNum::Ptr &sock) {
    //Start listening for socket writable events
    _sendable = false;
    int flag = _enable_recv ? EventPoller::Event_Read : 0;
    _poller->modifyEvent(sock->rawFd(), flag | EventPoller::Event_Error | EventPoller::Event_Write, [sock](bool) {});
}

void Socket::stopWriteAbleEvent(const SockNum::Ptr &sock) {
    //Stop listening for socket writable events
    _sendable = true;
    int flag = _enable_recv ? EventPoller::Event_Read : 0;
    _poller->modifyEvent(sock->rawFd(), flag | EventPoller::Event_Error, [sock](bool) {});
}

void Socket::enableRecv(bool enabled) {
    if (_enable_recv == enabled) {
        return;
    }
    _enable_recv = enabled;
    int read_flag = _enable_recv ? EventPoller::Event_Read : 0;
    //Do not listen for writable events when writable
    int send_flag = _sendable ? 0 : EventPoller::Event_Write;
    _poller->modifyEvent(rawFD(), read_flag | send_flag | EventPoller::Event_Error);
}

int Socket::rawFD() const {
    LOCK_GUARD(_mtx_sock_fd);
    if (!_sock_fd) {
        return -1;
    }
    return _sock_fd->rawFd();
}

bool Socket::alive() const {
    LOCK_GUARD(_mtx_sock_fd);
    return _sock_fd && !_err_emit;
}

SockNum::SockType Socket::sockType() const {
    LOCK_GUARD(_mtx_sock_fd);
    if (!_sock_fd) {
        return SockNum::Sock_Invalid;
    }
    return _sock_fd->type();
}

void Socket::setSendTimeOutSecond(uint32_t second) {
    _max_send_buffer_ms = second * 1000;
}

bool Socket::isSocketBusy() const {
    return !_sendable.load();
}

const EventPoller::Ptr &Socket::getPoller() const {
    return _poller;
}

std::shared_ptr<void> Socket::cloneSocket(const Socket &other) {
    closeSock();
    SockNum::Ptr sock;
    {
        LOCK_GUARD(other._mtx_sock_fd);
        if (!other._sock_fd) {
            WarnL << "sockfd of src socket is null";
            return nullptr;
        }
        sock = other._sock_fd->sockNum();
    }
    setSock(sock);
    std::weak_ptr<Socket> weak_self = shared_from_this();
    // 0x01 has no practical meaning and only represents success.
    return std::shared_ptr<void>(reinterpret_cast<void *>(0x01), [weak_self, sock](void *) {
        if (auto strong_self = weak_self.lock()) {
            if (!strong_self->attachEvent(sock)) {
                WarnL << "attachEvent failed: " << sock->rawFd();
            }
        }
    });
}

bool Socket::bindPeerAddr(const struct sockaddr *dst_addr, socklen_t addr_len, bool soft_bind) {
    LOCK_GUARD(_mtx_sock_fd);
    if (!_sock_fd) {
        return false;
    }
    if (_sock_fd->type() != SockNum::Sock_UDP) {
        return false;
    }
    addr_len = addr_len ? addr_len : SockUtil::get_sock_len(dst_addr);
    if (soft_bind) {
        //Soft bind, only save the address
        _udp_send_dst = std::make_shared<struct sockaddr_storage>();
        memcpy(_udp_send_dst.get(), dst_addr, addr_len);
    } else {
        //After hard binding, cancel soft binding to prevent performance loss of memcpy target address
        _udp_send_dst = nullptr;
        if (-1 == ::connect(_sock_fd->rawFd(), dst_addr, addr_len)) {
            WarnL << "Connect socket to peer address failed: " << SockUtil::inet_ntoa(dst_addr);
            return false;
        }
        memcpy(&_peer_addr, dst_addr, addr_len);
    }
    return true;
}

void Socket::setSendFlags(int flags) {
    _sock_flags = flags;
}

///////////////SockSender///////////////////

SockSender &SockSender::operator<<(const char *buf) {
    send(buf);
    return *this;
}

SockSender &SockSender::operator<<(string buf) {
    send(std::move(buf));
    return *this;
}

SockSender &SockSender::operator<<(Buffer::Ptr buf) {
    send(std::move(buf));
    return *this;
}

ssize_t SockSender::send(string buf) {
    return send(std::make_shared<BufferString>(std::move(buf)));
}

ssize_t SockSender::send(const char *buf, size_t size) {
    auto buffer = BufferRaw::create();
    buffer->assign(buf, size);
    return send(std::move(buffer));
}

///////////////SocketHelper///////////////////

SocketHelper::SocketHelper(const Socket::Ptr &sock) {
    setSock(sock);
    setOnCreateSocket(nullptr);
}

void SocketHelper::setPoller(const EventPoller::Ptr &poller) {
    _poller = poller;
}

void SocketHelper::setSock(const Socket::Ptr &sock) {
    _sock = sock;
    if (_sock) {
        _poller = _sock->getPoller();
    }
}

const EventPoller::Ptr &SocketHelper::getPoller() const {
    assert(_poller);
    return _poller;
}

const Socket::Ptr &SocketHelper::getSock() const {
    return _sock;
}

int SocketHelper::flushAll() {
    if (!_sock) {
        return -1;
    }
    return _sock->flushAll();
}

ssize_t SocketHelper::send(Buffer::Ptr buf) {
    if (!_sock) {
        return -1;
    }
    return _sock->send(std::move(buf), nullptr, 0, _try_flush);
}

ssize_t SocketHelper::sendto(Buffer::Ptr buf, struct sockaddr *addr, socklen_t addr_len) {
    if (!_sock) {
        return -1;
    }
    return _sock->send(std::move(buf), addr, addr_len, _try_flush);
}

void SocketHelper::shutdown(const SockException &ex) {
    if (_sock) {
        _sock->emitErr(ex);
    }
}

void SocketHelper::safeShutdown(const SockException &ex) {
    std::weak_ptr<SocketHelper> weak_self = shared_from_this();
    async_first([weak_self, ex]() {
        if (auto strong_self = weak_self.lock()) {
            strong_self->shutdown(ex);
        }
    });
}

string SocketHelper::get_local_ip() {
    return _sock ? _sock->get_local_ip() : "";
}

uint16_t SocketHelper::get_local_port() {
    return _sock ? _sock->get_local_port() : 0;
}

string SocketHelper::get_peer_ip() {
    return _sock ? _sock->get_peer_ip() : "";
}

uint16_t SocketHelper::get_peer_port() {
    return _sock ? _sock->get_peer_port() : 0;
}

const sockaddr * SocketHelper::get_peer_addr() {
    return _sock ? _sock->get_peer_addr() : nullptr;
}

const sockaddr *SocketHelper::get_local_addr() {
    return _sock ? _sock->get_local_addr() : nullptr;
}

bool SocketHelper::isSocketBusy() const {
    if (!_sock) {
        return true;
    }
    return _sock->isSocketBusy();
}

Task::Ptr SocketHelper::async(TaskIn task, bool may_sync) {
    return _poller->async(std::move(task), may_sync);
}

Task::Ptr SocketHelper::async_first(TaskIn task, bool may_sync) {
    return _poller->async_first(std::move(task), may_sync);
}

void SocketHelper::setSendFlushFlag(bool try_flush) {
    _try_flush = try_flush;
}

void SocketHelper::setSendFlags(int flags) {
    if (!_sock) {
        return;
    }
    _sock->setSendFlags(flags);
}

void SocketHelper::setOnCreateSocket(Socket::onCreateSocket cb) {
    if (cb) {
        _on_create_socket = std::move(cb);
    } else {
        _on_create_socket = [](const EventPoller::Ptr &poller) { return Socket::createSocket(poller, false); };
    }
}

Socket::Ptr SocketHelper::createSocket() {
    return _on_create_socket(_poller);
}

std::ostream &operator<<(std::ostream &ost, const SockException &err) {
    ost << err.getErrCode() << "(" << err.what() << ")";
    return ost;
}

} // namespace toolkit
