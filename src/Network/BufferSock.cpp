#include <assert.h>
#include "BufferSock.h"
#include "Util/logger.h"
#include "Util/uv_errno.h"

#if defined(__linux__) || defined(__linux)

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef MSG_WAITFORONE
#define MSG_WAITFORONE  0x10000
#endif

#ifndef HAVE_MMSG_HDR
struct mmsghdr {
        struct msghdr   msg_hdr;
        unsigned        msg_len;
};
#endif

#ifndef HAVE_SENDMMSG_API
#include <unistd.h>
#include <sys/syscall.h>
static inline int sendmmsg(int fd, struct mmsghdr *mmsg,
                unsigned vlen, unsigned flags)
{
        return syscall(__NR_sendmmsg, fd, mmsg, vlen, flags);
}
#endif

#ifndef HAVE_RECVMMSG_API
#include <unistd.h>
#include <sys/syscall.h>
static inline int recvmmsg(int fd, struct mmsghdr *mmsg,
                unsigned vlen, unsigned flags, struct timespec *timeout)
{
        return syscall(__NR_recvmmsg, fd, mmsg, vlen, flags, timeout);
}
#endif

#endif// defined(__linux__) || defined(__linux)

namespace toolkit {

StatisticImp(BufferList)

/////////////////////////////////////// BufferSock ///////////////////////////////////////

BufferSock::BufferSock(Buffer::Ptr buffer, struct sockaddr *addr, int addr_len) {
    if (addr) {
        _addr_len = addr_len ? addr_len : SockUtil::get_sock_len(addr);
        memcpy(&_addr, addr, _addr_len);
    }
    assert(buffer);
    _buffer = std::move(buffer);
}

char *BufferSock::data() const {
    return _buffer->data();
}

size_t BufferSock::size() const {
    return _buffer->size();
}

const struct sockaddr *BufferSock::sockaddr() const {
    return (struct sockaddr *)&_addr;
}

socklen_t BufferSock::socklen() const {
    return _addr_len;
}

/////////////////////////////////////// BufferCallBack ///////////////////////////////////////

class BufferCallBack {
public:
    BufferCallBack(List<std::pair<Buffer::Ptr, bool> > list, BufferList::SendResult cb)
        : _cb(std::move(cb))
        , _pkt_list(std::move(list)) {}

    ~BufferCallBack() {
        sendCompleted(false);
    }

    void sendCompleted(bool flag) {
        if (_cb) {
            //All send success or failure callback
            while (!_pkt_list.empty()) {
                _cb(_pkt_list.front().first, flag);
                _pkt_list.pop_front();
            }
        } else {
            _pkt_list.clear();
        }
    }

    void sendFrontSuccess() {
        if (_cb) {
            //Send success callback
            _cb(_pkt_list.front().first, true);
        }
        _pkt_list.pop_front();
    }

protected:
    BufferList::SendResult _cb;
    List<std::pair<Buffer::Ptr, bool> > _pkt_list;
};

/////////////////////////////////////// BufferSendMsg ///////////////////////////////////////
#if defined(_WIN32)
using SocketBuf = WSABUF;
#else
using SocketBuf = iovec;
#endif

class BufferSendMsg final : public BufferList, public BufferCallBack {
public:
    using SocketBufVec = std::vector<SocketBuf>;

    BufferSendMsg(List<std::pair<Buffer::Ptr, bool> > list, SendResult cb);
    ~BufferSendMsg() override = default;

    bool empty() override;
    size_t count() override;
    ssize_t send(int fd, int flags) override;

private:
    void reOffset(size_t n);
    ssize_t send_l(int fd, int flags);

private:
    size_t _iovec_off = 0;
    size_t _remain_size = 0;
    SocketBufVec _iovec;
};

bool BufferSendMsg::empty() {
    return _remain_size == 0;
}

size_t BufferSendMsg::count() {
    return _iovec.size() - _iovec_off;
}

ssize_t BufferSendMsg::send_l(int fd, int flags) {
    ssize_t n;  
#if !defined(_WIN32)
    do {
        struct msghdr msg;
        msg.msg_name = nullptr;
        msg.msg_namelen = 0;
        msg.msg_iov = &(_iovec[_iovec_off]);
        msg.msg_iovlen = _iovec.size() - _iovec_off;
        if (msg.msg_iovlen > IOV_MAX) {
            msg.msg_iovlen = IOV_MAX;
        }
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
        msg.msg_flags = flags;
        n = sendmsg(fd, &msg, flags);
    } while (-1 == n && UV_EINTR == get_uv_error(true));
#else
    do {
        DWORD sent = 0;
        n = WSASend(fd, const_cast<LPWSABUF>(&_iovec[0]), static_cast<DWORD>(_iovec.size()), &sent, static_cast<DWORD>(flags), 0, 0);
        if (n == SOCKET_ERROR) {return -1;}
        n = sent;
    } while (n < 0 && UV_ECANCELED == get_uv_error(true));
#endif

    if (n >= (ssize_t)_remain_size) {
        //All written
        _remain_size = 0;
        sendCompleted(true);
        return n;
    }

    if (n > 0) {
        //Partial send success
        reOffset(n);
        return n;
    }

    //Not a single byte sent
    return n;
}

ssize_t BufferSendMsg::send(int fd, int flags) {
    auto remain_size = _remain_size;
    while (_remain_size && send_l(fd, flags) != -1);

    ssize_t sent = remain_size - _remain_size;
    if (sent > 0) {
        //Partial or all send success
        return sent;
    }
    //Not a single byte sent successfully
    return -1;
}

void BufferSendMsg::reOffset(size_t n) {
    _remain_size -= n;
    size_t offset = 0;
    for (auto i = _iovec_off; i != _iovec.size(); ++i) {
        auto &ref = _iovec[i];
#if !defined(_WIN32)
        offset += ref.iov_len;
#else
        offset += ref.len;
#endif
        if (offset < n) {
            //This package is sent
            sendFrontSuccess();
            continue;
        }
        _iovec_off = i;
        if (offset == n) {
            //This is the last package sent
            ++_iovec_off;
            sendFrontSuccess();
            break;
        }
        //This is the last package partially sent
        size_t remain = offset - n;
#if !defined(_WIN32)
        ref.iov_base = (char *)ref.iov_base + ref.iov_len - remain;
        ref.iov_len = remain;
#else
        ref.buf = (CHAR *)ref.buf + ref.len - remain;
        ref.len = remain;
#endif
        break;
    }
}

BufferSendMsg::BufferSendMsg(List<std::pair<Buffer::Ptr, bool>> list, SendResult cb)
    : BufferCallBack(std::move(list), std::move(cb))
    , _iovec(_pkt_list.size()) {
    auto it = _iovec.begin();
    _pkt_list.for_each([&](std::pair<Buffer::Ptr, bool> &pr) {
#if !defined(_WIN32)
        it->iov_base = pr.first->data();
        it->iov_len = pr.first->size();
        _remain_size += it->iov_len;
#else
        it->buf = pr.first->data();
        it->len = pr.first->size();
        _remain_size += it->len;
#endif
        ++it;
    });
}

/////////////////////////////////////// BufferSendTo ///////////////////////////////////////
class BufferSendTo final: public BufferList, public BufferCallBack {
public:
    BufferSendTo(List<std::pair<Buffer::Ptr, bool> > list, SendResult cb, bool is_udp);
    ~BufferSendTo() override = default;

    bool empty() override;
    size_t count() override;
    ssize_t send(int fd, int flags) override;

private:
    bool _is_udp;
    size_t _offset = 0;
};

BufferSendTo::BufferSendTo(List<std::pair<Buffer::Ptr, bool>> list, BufferList::SendResult cb, bool is_udp)
    : BufferCallBack(std::move(list), std::move(cb))
    , _is_udp(is_udp) {}

bool BufferSendTo::empty() {
    return _pkt_list.empty();
}

size_t BufferSendTo::count() {
    return _pkt_list.size();
}

static inline BufferSock *getBufferSockPtr(std::pair<Buffer::Ptr, bool> &pr) {
    if (!pr.second) {
        return nullptr;
    }
    return static_cast<BufferSock *>(pr.first.get());
}

ssize_t BufferSendTo::send(int fd, int flags) {
    size_t sent = 0;
    ssize_t n;
    while (!_pkt_list.empty()) {
        auto &front = _pkt_list.front();
        auto &buffer = front.first;
        if (_is_udp) {
            auto ptr = getBufferSockPtr(front);
            n = ::sendto(fd, buffer->data() + _offset, buffer->size() - _offset, flags, ptr ? ptr->sockaddr() : nullptr, ptr ? ptr->socklen() : 0);
        } else {
            n = ::send(fd, buffer->data() + _offset, buffer->size() - _offset, flags);
        }

        if (n >= 0) {
            assert(n);
            _offset += n;
            if (_offset == buffer->size()) {
                sendFrontSuccess();
                _offset = 0;
            }
            sent += n;
            continue;
        }

        //n == -1 case
        if (get_uv_error(true) == UV_EINTR) {
            //interrupted, need to continue sending
            continue;
        }
        //other reasons causing send to return -1
        break;
    }
    return sent ? sent : -1;
}

/////////////////////////////////////// BufferSendMmsg ///////////////////////////////////////

#if defined(__linux__) || defined(__linux)

class BufferSendMMsg : public BufferList, public BufferCallBack {
public:
    BufferSendMMsg(List<std::pair<Buffer::Ptr, bool> > list, SendResult cb);
    ~BufferSendMMsg() override = default;

    bool empty() override;
    size_t count() override;
    ssize_t send(int fd, int flags) override;

private:
    void reOffset(size_t n);
    ssize_t send_l(int fd, int flags);

private:
    size_t _remain_size = 0;
    std::vector<struct iovec> _iovec;
    std::vector<struct mmsghdr> _hdrvec;
};

bool BufferSendMMsg::empty() {
    return _remain_size == 0;
}

size_t BufferSendMMsg::count() {
    return _hdrvec.size();
}

ssize_t BufferSendMMsg::send_l(int fd, int flags) {
    ssize_t n;
    do {
        n = sendmmsg(fd, &_hdrvec[0], _hdrvec.size(), flags);
    } while (-1 == n && UV_EINTR == get_uv_error(true));

    if (n > 0) {
        //partially or fully sent successfully
        reOffset(n);
        return n;
    }

    //not a single byte sent
    return n;
}

ssize_t BufferSendMMsg::send(int fd, int flags) {
    auto remain_size = _remain_size;
    while (_remain_size && send_l(fd, flags) != -1);
    ssize_t sent = remain_size - _remain_size;
    if (sent > 0) {
        //partially or fully sent successfully
        return sent;
    }
    //not a single byte sent successfully
    return -1;
}

void BufferSendMMsg::reOffset(size_t n) {
    for (auto it = _hdrvec.begin(); it != _hdrvec.end();) {
        auto &hdr = *it;
        auto &io = *(hdr.msg_hdr.msg_iov);
        assert(hdr.msg_len <= io.iov_len);
        _remain_size -= hdr.msg_len;
        if (hdr.msg_len == io.iov_len) {
            //this UDP packet sent successfully
            it = _hdrvec.erase(it);
            sendFrontSuccess();
            continue;
        }
        //partially sent successfully
        io.iov_base = (char *)io.iov_base + hdr.msg_len;
        io.iov_len -= hdr.msg_len;
        break;
    }
}

BufferSendMMsg::BufferSendMMsg(List<std::pair<Buffer::Ptr, bool>> list, SendResult cb)
    : BufferCallBack(std::move(list), std::move(cb))
    , _iovec(_pkt_list.size())
    , _hdrvec(_pkt_list.size()) {
    auto i = 0U;
    _pkt_list.for_each([&](std::pair<Buffer::Ptr, bool> &pr) {
        auto &io = _iovec[i];
        io.iov_base = pr.first->data();
        io.iov_len = pr.first->size();
        _remain_size += io.iov_len;

        auto ptr = getBufferSockPtr(pr);
        auto &mmsg = _hdrvec[i];
        auto &msg = mmsg.msg_hdr;
        mmsg.msg_len = 0;
        msg.msg_name = ptr ? (void *)ptr->sockaddr() : nullptr;
        msg.msg_namelen = ptr ? ptr->socklen() : 0;
        msg.msg_iov = &io;
        msg.msg_iovlen = 1;
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;
        ++i;
    });
}

#endif //defined(__linux__) || defined(__linux)


BufferList::Ptr BufferList::create(List<std::pair<Buffer::Ptr, bool> > list, SendResult cb, bool is_udp) {
#if defined(_WIN32)
    if (is_udp) {
        //sendto/send scheme, to be optimized
        return std::make_shared<BufferSendTo>(std::move(list), std::move(cb), is_udp);
    }
    //WSASend scheme
    return std::make_shared<BufferSendMsg>(std::move(list), std::move(cb));
#elif defined(__linux__) || defined(__linux)
    if (is_udp) {
        //sendmmsg scheme
        return std::make_shared<BufferSendMMsg>(std::move(list), std::move(cb));
    }
    //sendmsg scheme
    return std::make_shared<BufferSendMsg>(std::move(list), std::move(cb));
#else
    if (is_udp) {
        //sendto/send scheme, can be optimized?
        return std::make_shared<BufferSendTo>(std::move(list), std::move(cb), is_udp);
    }
    //sendmsg scheme
    return std::make_shared<BufferSendMsg>(std::move(list), std::move(cb));
#endif
}

#if defined(__linux) || defined(__linux__)
class SocketRecvmmsgBuffer : public SocketRecvBuffer {
public:
    SocketRecvmmsgBuffer(size_t count, size_t size)
        : _size(size)
        , _iovec(count)
        , _mmsgs(count)
        , _buffers(count)
        , _address(count) {
        for (auto i = 0u; i < count; ++i) {
            auto buf = BufferRaw::create();
            buf->setCapacity(size);

            _buffers[i] = buf;
            auto &mmsg = _mmsgs[i];
            auto &addr = _address[i];
            mmsg.msg_len = 0;
            mmsg.msg_hdr.msg_name = &addr;
            mmsg.msg_hdr.msg_namelen = sizeof(addr);
            mmsg.msg_hdr.msg_iov = &_iovec[i];
            mmsg.msg_hdr.msg_iov->iov_base = buf->data();
            mmsg.msg_hdr.msg_iov->iov_len = buf->getCapacity() - 1;
            mmsg.msg_hdr.msg_iovlen = 1;
            mmsg.msg_hdr.msg_control = nullptr;
            mmsg.msg_hdr.msg_controllen = 0;
            mmsg.msg_hdr.msg_flags = 0;
        }
    }

    ssize_t recvFromSocket(int fd, ssize_t &count) override {
        for (auto i = 0; i < _last_count; ++i) {
            auto &mmsg = _mmsgs[i];
            mmsg.msg_hdr.msg_namelen = sizeof(struct sockaddr_storage);
            auto &buf = _buffers[i];
            if (!buf) {
                auto raw = BufferRaw::create();
                raw->setCapacity(_size);
                buf = raw;
                mmsg.msg_hdr.msg_iov->iov_base = buf->data();
            }
        }
        do {
            count = recvmmsg(fd, &_mmsgs[0], _mmsgs.size(), 0, nullptr);
        } while (-1 == count && UV_EINTR == get_uv_error(true));

        _last_count = count;
        if (count <= 0) {
            return count;
        }

        ssize_t nread = 0;
        for (auto i = 0; i < count; ++i) {
            auto &mmsg = _mmsgs[i];
            nread += mmsg.msg_len;

            auto buf = std::static_pointer_cast<BufferRaw>(_buffers[i]);
            buf->setSize(mmsg.msg_len);
            buf->data()[mmsg.msg_len] = '\0';
        }
        return nread;
    }

    Buffer::Ptr &getBuffer(size_t index) override { return _buffers[index]; }

    struct sockaddr_storage &getAddress(size_t index) override { return _address[index]; }

private:
    size_t _size;
    ssize_t _last_count { 0 };
    std::vector<struct iovec> _iovec;
    std::vector<struct mmsghdr> _mmsgs;
    std::vector<Buffer::Ptr> _buffers;
    std::vector<struct sockaddr_storage> _address;
};
#endif

class SocketRecvFromBuffer : public SocketRecvBuffer {
public:
    SocketRecvFromBuffer(size_t size): _size(size) {}
    
    ssize_t recvFromSocket(int fd, ssize_t &count) override {
        ssize_t nread;
        socklen_t len = sizeof(_address);
        if (!_buffer) {
            allocBuffer();
        }

        do {
            nread = recvfrom(fd, _buffer->data(), _buffer->getCapacity() - 1, 0, (struct sockaddr *)&_address, &len);
        } while (-1 == nread && UV_EINTR == get_uv_error(true));

        if (nread > 0) {
            count = 1;
            _buffer->data()[nread] = '\0';
            std::static_pointer_cast<BufferRaw>(_buffer)->setSize(nread);
        }
        return nread;
    }

    Buffer::Ptr &getBuffer(size_t index) override { return _buffer; }

    struct sockaddr_storage &getAddress(size_t index) override { return _address; }

private:
    void allocBuffer() {
        auto buf = BufferRaw::create();
        buf->setCapacity(_size);
        _buffer = std::move(buf);
    }

private:
    size_t _size;
    Buffer::Ptr _buffer;
    struct sockaddr_storage _address;
};

static constexpr auto kPacketCount = 32;
static constexpr auto kBufferCapacity = 4 * 1024u;

SocketRecvBuffer::Ptr SocketRecvBuffer::create(bool is_udp) {
#if defined(__linux) || defined(__linux__)
    if (is_udp) {
        return std::make_shared<SocketRecvmmsgBuffer>(kPacketCount, kBufferCapacity);
    }
#endif
    return std::make_shared<SocketRecvFromBuffer>(kPacketCount * kBufferCapacity);
}

} //toolkit
