#ifndef NETWORK_SOCKET_H
#define NETWORK_SOCKET_H

#include <memory>
#include <string>
#include <mutex>
#include <atomic>
#include <sstream>
#include <functional>
#include "Util/SpeedStatistic.h"
#include "sockutil.h"
#include "Poller/Timer.h"
#include "Poller/EventPoller.h"
#include "BufferSock.h"

namespace toolkit {

#if defined(MSG_NOSIGNAL)
#define FLAG_NOSIGNAL MSG_NOSIGNAL
#else
#define FLAG_NOSIGNAL 0
#endif //MSG_NOSIGNAL

#if defined(MSG_MORE)
#define FLAG_MORE MSG_MORE
#else
#define FLAG_MORE 0
#endif //MSG_MORE

#if defined(MSG_DONTWAIT)
#define FLAG_DONTWAIT MSG_DONTWAIT
#else
#define FLAG_DONTWAIT 0
#endif //MSG_DONTWAIT

//Default socket flags: do not trigger SIGPIPE, non-blocking send
#define SOCKET_DEFAULE_FLAGS (FLAG_NOSIGNAL | FLAG_DONTWAIT )
    
//Send timeout time, if no data is sent successfully within the specified time, the onErr event will be triggered
#define SEND_TIME_OUT_SEC 10
    
//Error type enumeration
typedef enum {
    Err_success = 0, // success
    Err_eof, //eof
    Err_timeout, // socket timeout
    Err_refused,// socket refused
    Err_reset,//  socket reset
    Err_dns,// dns resolve failed
    Err_shutdown,// socket shutdown
    Err_other = 0xFF,// other error
} ErrCode;

//Error message class
class SockException : public std::exception {
public:
    SockException(ErrCode code = Err_success, const std::string &msg = "", int custom_code = 0) {
        _msg = msg;
        _code = code;
        _custom_code = custom_code;
    }

    //Reset error
    void reset(ErrCode code, const std::string &msg, int custom_code = 0) {
        _msg = msg;
        _code = code;
        _custom_code = custom_code;
    }

    //Error prompt
    const char *what() const noexcept override {
        return _msg.c_str();
    }

    //Error code
    ErrCode getErrCode() const {
        return _code;
    }

    //User-defined error code
    int getCustomCode() const {
        return _custom_code;
    }

    //Determine if there is really an error
    operator bool() const {
        return _code != Err_success;
    }

private:
    ErrCode _code;
    int _custom_code = 0;
    std::string _msg;
};

//std::cout and other output streams can directly output SockException objects
std::ostream &operator<<(std::ostream &ost, const SockException &err);

class SockNum {
public:
    using Ptr = std::shared_ptr<SockNum>;

    typedef enum {
        Sock_Invalid = -1,
        Sock_TCP = 0,
        Sock_UDP = 1,
        Sock_TCP_Server = 2
    } SockType;

    SockNum(int fd, SockType type) {
        _fd = fd;
        _type = type;
    }

    ~SockNum() {
#if defined (OS_IPHONE)
        unsetSocketOfIOS(_fd);
#endif //OS_IPHONE
        //Stop socket send and receive capability
        #if defined(_WIN32)
        ::shutdown(_fd, SD_BOTH);
        #else
        ::shutdown(_fd, SHUT_RDWR);
        #endif
        close(_fd);
    }

    int rawFd() const {
        return _fd;
    }

    SockType type() {
        return _type;
    }

    void setConnected() {
#if defined (OS_IPHONE)
        setSocketOfIOS(_fd);
#endif //OS_IPHONE
    }

#if defined (OS_IPHONE)
private:
    void *readStream=nullptr;
    void *writeStream=nullptr;
    bool setSocketOfIOS(int socket);
    void unsetSocketOfIOS(int socket);
#endif //OS_IPHONE

private:
    int _fd;
    SockType _type;
};

//Socket file descriptor wrapper
//Automatically overflow listening and close socket when destructing
//Prevent descriptor overflow
class SockFD : public noncopyable {
public:
    using Ptr = std::shared_ptr<SockFD>;

    /**
     * Create an fd object
     * @param num File descriptor, int number
     * @param poller Event listener
     */
    SockFD(SockNum::Ptr num, EventPoller::Ptr poller) {
        _num = std::move(num);
        _poller = std::move(poller);
    }

    /**
     * Copy an fd object
     * @param that Source object
     * @param poller Event listener
     */
    SockFD(const SockFD &that, EventPoller::Ptr poller) {
        _num = that._num;
        _poller = std::move(poller);
        if (_poller == that._poller) {
            throw std::invalid_argument("Copy a SockFD with same poller");
        }
    }

     ~SockFD() { delEvent(); }

    void delEvent() {
        if (_poller) {
            auto num = _num;
            //Remove IO event successfully before closing fd
            _poller->delEvent(num->rawFd(), [num](bool) {});
            _poller = nullptr;
        }
    }

    void setConnected() {
        _num->setConnected();
    }

    int rawFd() const {
        return _num->rawFd();
    }

    const SockNum::Ptr& sockNum() const {
        return _num;
    }

    SockNum::SockType type() {
        return _num->type();
    }

    const EventPoller::Ptr& getPoller() const {
        return _poller;
    }

private:
    SockNum::Ptr _num;
    EventPoller::Ptr _poller;
};

template<class Mtx = std::recursive_mutex>
class MutexWrapper {
public:
    MutexWrapper(bool enable) {
        _enable = enable;
    }

    ~MutexWrapper() = default;

    inline void lock() {
        if (_enable) {
            _mtx.lock();
        }
    }

    inline void unlock() {
        if (_enable) {
            _mtx.unlock();
        }
    }

private:
    bool _enable;
    Mtx _mtx;
};

class SockInfo {
public:
    SockInfo() = default;
    virtual ~SockInfo() = default;

    //Get local IP
    virtual std::string get_local_ip() = 0;
    //Get local port number
    virtual uint16_t get_local_port() = 0;
    //Get peer IP
    virtual std::string get_peer_ip() = 0;
    //Get the peer's port number
    virtual uint16_t get_peer_port() = 0;
    //Get the identifier
    virtual std::string getIdentifier() const { return ""; }
};

#define TraceP(ptr) TraceL << ptr->getIdentifier() << "(" << ptr->get_peer_ip() << ":" << ptr->get_peer_port() << ") "
#define DebugP(ptr) DebugL << ptr->getIdentifier() << "(" << ptr->get_peer_ip() << ":" << ptr->get_peer_port() << ") "
#define InfoP(ptr) InfoL << ptr->getIdentifier() << "(" << ptr->get_peer_ip() << ":" << ptr->get_peer_port() << ") "
#define WarnP(ptr) WarnL << ptr->getIdentifier() << "(" << ptr->get_peer_ip() << ":" << ptr->get_peer_port() << ") "
#define ErrorP(ptr) ErrorL << ptr->getIdentifier() << "(" << ptr->get_peer_ip() << ":" << ptr->get_peer_port() << ") "

//Asynchronous IO Socket object, including TCP client, server, and UDP socket
class Socket : public std::enable_shared_from_this<Socket>, public noncopyable, public SockInfo {
public:
    using Ptr = std::shared_ptr<Socket>;
    //Receive data callback
    using onReadCB = std::function<void(Buffer::Ptr &buf, struct sockaddr *addr, int addr_len)>;
    using onMultiReadCB = toolkit::function_safe<void(Buffer::Ptr *buf, struct sockaddr_storage *addr, size_t count)>;

    //Error callback
    using onErrCB = toolkit::function_safe<void(const SockException &err)>;
    //TCP listen receives a connection request
    using onAcceptCB = toolkit::function_safe<void(Socket::Ptr &sock, std::shared_ptr<void> &complete)>;
    //Socket send buffer is cleared event, returns true to continue listening for the event next time, otherwise stops
    using onFlush = toolkit::function_safe<bool()>;
    //Intercept the default generation method of the Socket before receiving a connection request
    using onCreateSocket = toolkit::function_safe<Ptr(const EventPoller::Ptr &poller)>;
    //Send buffer success or failure callback
    using onSendResult = BufferList::SendResult;

    /**
     * Construct a socket object, no actual operation yet
     * @param poller The bound poller thread
     * @param enable_mutex Whether to enable the mutex (whether the interface is thread-safe)
    */
    static Ptr createSocket(const EventPoller::Ptr &poller = nullptr, bool enable_mutex = true);
    ~Socket() override;

    /**
     * Create a TCP client and connect to the server asynchronously
     * @param url Target server IP or domain name
     * @param port Target server port
     * @param con_cb Result callback
     * @param timeout_sec Timeout time
     * @param local_ip Local network card IP to bind
     * @param local_port Local network card port number to bind
     */
    void connect(const std::string &url, uint16_t port, const onErrCB &con_cb, float timeout_sec = 5, const std::string &local_ip = "::", uint16_t local_port = 0);

    /**
     * Create a TCP listening server
     * @param port Listening port, 0 for random
     * @param local_ip Network card IP to listen on
     * @param backlog Maximum TCP backlog
     * @return Whether successful
     */
    bool listen(uint16_t port, const std::string &local_ip = "::", int backlog = 1024);

    /**
     * Create a UDP socket, UDP is connectionless, so it can be used as a server and client
     * @param port Port to bind, 0 for random
     * @param local_ip Network card IP to bind
     * @return Whether successful
     */
    bool bindUdpSock(uint16_t port, const std::string &local_ip = "::", bool enable_reuse = true);

    /**
     * Wrap an external file descriptor, this object is responsible for closing the file descriptor
     * Internally, the file descriptor will be set to NoBlocked, NoSigpipe, CloExec
     * Other settings need to be set manually using SockUtil
     */
    bool fromSock(int fd, SockNum::SockType type);

    /**
     * Clone from another Socket
     * The purpose is to allow a socket to be listened to by multiple poller objects, improving performance or implementing socket migration between threads
     * @param other Original socket object
     * @return Whether successful
     */
    std::shared_ptr<void> cloneSocket(const Socket &other);

    /**
     * Switch poller thread, note that it can only be called before onAccept
     * @param poller new thread
     */
    void moveTo(EventPoller::Ptr poller);

    //////////// Set event callbacks ////////////

    /**
     * Set data receive callback, valid for TCP or UDP clients
     * @param cb Callback object
     */
    void setOnRead(onReadCB cb);
    void setOnMultiRead(onMultiReadCB cb);

    /**
     * Set exception event (including EOF) callback
     * @param cb Callback object
     */
    void setOnErr(onErrCB cb);

    /**
     * Set TCP listening receive connection callback
     * @param cb Callback object
     */
    void setOnAccept(onAcceptCB cb);

    /**
     * Set socket write buffer clear event callback
     * This callback can be used to implement send flow control
     * @param cb Callback object
     */
    void setOnFlush(onFlush cb);

    /**
     * Set accept callback when socket is constructed
     * @param cb callback
     */
    void setOnBeforeAccept(onCreateSocket cb);

    /**
     * Set send buffer result callback
     * @param cb callback
     */
    void setOnSendResult(onSendResult cb);

    ////////////Data sending related interfaces////////////

    /**
     * Send data pointer
     * @param buf data pointer
     * @param size data length
     * @param addr target address
     * @param addr_len target address length
     * @param try_flush whether to try writing to the socket
     * @return -1 represents failure (invalid socket), 0 represents data length is 0, otherwise returns data length
     */
    ssize_t send(const char *buf, size_t size = 0, struct sockaddr *addr = nullptr, socklen_t addr_len = 0, bool try_flush = true);

    /**
     * Send string
     */
    ssize_t send(std::string buf, struct sockaddr *addr = nullptr, socklen_t addr_len = 0, bool try_flush = true);

    /**
     * Send Buffer object, unified exit for Socket object to send data
     * unified exit for Socket object to send data
     */
    ssize_t send(Buffer::Ptr buf, struct sockaddr *addr = nullptr, socklen_t addr_len = 0, bool try_flush = true);

    /**
     * Try to write all data to the socket
     * @return -1 represents failure (invalid socket or send timeout), 0 represents success?
     */
    int flushAll();

    /**
     * Close the socket and trigger the onErr callback, the onErr callback will be executed in the poller thread
     * @param err error reason
     * @return whether the onErr callback is successfully triggered
     */
    bool emitErr(const SockException &err) noexcept;

    /**
     * Enable or disable data reception
     * @param enabled whether to enable
     */
    void enableRecv(bool enabled);

    /**
     * Get the raw file descriptor, do not perform close operation (because the Socket object will manage its lifecycle)
     * @return file descriptor
     */
    int rawFD() const;

    /**
     * Whether the TCP client is in a connected state
     * Supports Sock_TCP type socket
     */
    bool alive() const;

    /**
     * Returns the socket type
     */
    SockNum::SockType sockType() const;

    /**
     * Sets the send timeout to disconnect actively; default 10 seconds
     * @param second Send timeout data, in seconds
     */
    void setSendTimeOutSecond(uint32_t second);

    /**
     * Whether the socket is busy, if the socket write buffer is full, returns true
     * @return Whether the socket is busy
     */
    bool isSocketBusy() const;

    /**
     * Gets the poller thread object
     * @return poller thread object
     */
    const EventPoller::Ptr &getPoller() const;

    /**
     * Binds the UDP target address, subsequent sends do not need to specify it separately
     * @param dst_addr Target address
     * @param addr_len Target address length
     * @param soft_bind Whether to soft bind, soft binding does not call the UDP connect interface, only saves the target address information, and sends it to the sendto function
     * @return Whether successful
     */
    bool bindPeerAddr(const struct sockaddr *dst_addr, socklen_t addr_len = 0, bool soft_bind = false);

    /**
     * Sets the send flags
     * @param flags Send flags
     */
    void setSendFlags(int flags = SOCKET_DEFAULE_FLAGS);

    /**
     * Closes the socket
     * @param close_fd Whether to close the fd or only remove the IO event listener
     */
    void closeSock(bool close_fd = true);

    /**
     * Gets the number of packets in the send buffer (not the number of bytes)
     */
    size_t getSendBufferCount();

    /**
     * Gets the number of milliseconds since the last socket send buffer was cleared, in milliseconds
     */
    uint64_t elapsedTimeAfterFlushed();

    /**
     * Get the receiving rate, in bytes/s
     */
    size_t getRecvSpeed();

    /**
     * Get the sending rate, in bytes/s
     */
    size_t getSendSpeed();

    /**
     * Get the total recv bytes
     */
    size_t getRecvTotalBytes();

    /**
     * Get the total send bytes
     */
    size_t getSendTotalBytes();

    ////////////SockInfo override////////////
    std::string get_local_ip() override;
    uint16_t get_local_port() override;
    std::string get_peer_ip() override;
    uint16_t get_peer_port() override;
    std::string getIdentifier() const override;
    const sockaddr *get_peer_addr();
    const sockaddr *get_local_addr();

private:
    Socket(EventPoller::Ptr poller, bool enable_mutex = true);

    void setSock(SockNum::Ptr sock);
    int onAccept(const SockNum::Ptr &sock, int event) noexcept;
    ssize_t onRead(const SockNum::Ptr &sock, const SocketRecvBuffer::Ptr &buffer) noexcept;
    void onWriteAble(const SockNum::Ptr &sock);
    void onConnected(const SockNum::Ptr &sock, const onErrCB &cb);
    void onFlushed();
    void startWriteAbleEvent(const SockNum::Ptr &sock);
    void stopWriteAbleEvent(const SockNum::Ptr &sock);
    bool flushData(const SockNum::Ptr &sock, bool poller_thread);
    bool attachEvent(const SockNum::Ptr &sock);
    ssize_t send_l(Buffer::Ptr buf, bool is_buf_sock, bool try_flush = true);
    void connect_l(const std::string &url, uint16_t port, const onErrCB &con_cb_in, float timeout_sec, const std::string &local_ip, uint16_t local_port);
    bool fromSock_l(SockNum::Ptr sock);

private:
    //Flag for sending socket
    int _sock_flags = SOCKET_DEFAULE_FLAGS;
    //Maximum send buffer, in milliseconds, the time since the last send buffer was cleared cannot exceed this parameter
    uint32_t _max_send_buffer_ms = SEND_TIME_OUT_SEC * 1000;
    //Control whether to receive listen socket readable events, can be used for traffic control after closing
    std::atomic<bool> _enable_recv { true };
    //Mark whether the socket is writable, the socket write buffer is full and cannot be written
    std::atomic<bool> _sendable { true };
    //Whether the err callback has been triggered
    bool _err_emit = false;
    //Whether to enable network speed statistics
    bool _enable_speed = false;
    //UDP send target address
    std::shared_ptr<struct sockaddr_storage> _udp_send_dst;

    //Receiving rate statistics
    BytesSpeed _recv_speed;
    //Send rate statistics
    BytesSpeed _send_speed;

    //TCP connection timeout timer
    Timer::Ptr _con_timer;
    //TCP connection result callback object
    std::shared_ptr<void> _async_con_cb;

    //Record the timer for the last send buffer (including socket write buffer and application layer buffer) cleared
    Ticker _send_flush_ticker;
    //Abstract class for socket fd
    SockFD::Ptr _sock_fd;
    //The poller thread bound to this socket, events are triggered in this thread
    EventPoller::Ptr _poller;
    //Need to lock when accessing _sock_fd across threads
    mutable MutexWrapper<std::recursive_mutex> _mtx_sock_fd;

    //Socket exception event (such as disconnection)
    onErrCB _on_err;
    //Receive data event
    onMultiReadCB _on_multi_read;
    //Socket buffer cleared event (can be used for send flow control)
    onFlush _on_flush;
    //TCP listener receives an accept request event
    onAcceptCB _on_accept;
    //TCP listener receives an accept request, custom creation of peer Socket event (can control binding of child Socket to other poller threads)
    onCreateSocket _on_before_accept;
    //Set the lock for the above callback function
    MutexWrapper<std::recursive_mutex> _mtx_event;

    //First-level send cache, when the socket is writable, it will batch the first-level cache into the second-level cache
    List<std::pair<Buffer::Ptr, bool>> _send_buf_waiting;
    //First-level send cache lock
    MutexWrapper<std::recursive_mutex> _mtx_send_buf_waiting;
    //Second-level send cache, when the socket is writable, it will batch the second-level cache into the socket
    List<BufferList::Ptr> _send_buf_sending;
    //Second-level send cache lock
    MutexWrapper<std::recursive_mutex> _mtx_send_buf_sending;
    //Send buffer result callback
    BufferList::SendResult _send_result;
    //Object count statistics
    ObjectStatistic<Socket> _statistic;

    //Connection cache address, to prevent TCP reset from causing the inability to obtain the peer's address
    struct sockaddr_storage _local_addr;
    struct sockaddr_storage _peer_addr;
};

class SockSender {
public:
    SockSender() = default;
    virtual ~SockSender() = default;
    virtual ssize_t send(Buffer::Ptr buf) = 0;
    virtual ssize_t sendto(Buffer::Ptr buf, struct sockaddr *addr = nullptr, socklen_t addr_len = 0) = 0;
    virtual void shutdown(const SockException &ex = SockException(Err_shutdown, "self shutdown")) = 0;

    //Send char *
    SockSender &operator << (const char *buf);
    //Send string
    SockSender &operator << (std::string buf);
    //Send Buffer object
    SockSender &operator << (Buffer::Ptr buf);

    //Send other types of data
    template<typename T>
    SockSender &operator << (T &&buf) {
        std::ostringstream ss;
        ss << std::forward<T>(buf);
        send(ss.str());
        return *this;
    }

    ssize_t send(std::string buf);
    ssize_t send(const char *buf, size_t size = 0);
};

//Socket object wrapper class
class SocketHelper : public SockSender, public SockInfo, public TaskExecutorInterface, public std::enable_shared_from_this<SocketHelper> {
public:
    using Ptr = std::shared_ptr<SocketHelper>;
    SocketHelper(const Socket::Ptr &sock);
    ~SocketHelper() override = default;

    ///////////////////// Socket util std::functions /////////////////////
    /**
     * Get poller thread
     */
    const EventPoller::Ptr& getPoller() const;

    /**
     * Set batch send flag, used to improve performance
     * @param try_flush Batch send flag
     */
    void setSendFlushFlag(bool try_flush);

    /**
     * Set socket send flags
     * @param flags Socket send flags
     */
    void setSendFlags(int flags);

    /**
     * Whether the socket is busy, returns true if the socket write buffer is full
     */
    bool isSocketBusy() const;

    /**
     * Set Socket creator, customize Socket creation method
     * @param cb Creator
     */
    void setOnCreateSocket(Socket::onCreateSocket cb);

    /**
     * Create a socket object
     */
    Socket::Ptr createSocket();

    /**
     * Get the socket object
     */
    const Socket::Ptr &getSock() const;

    /**
     * Try to write all data to the socket
     * @return -1 represents failure (invalid socket or send timeout), 0 represents success
     */
    int flushAll();

    /**
     * Whether SSL encryption is enabled
     */
    virtual bool overSsl() const { return false; }

    ///////////////////// SockInfo override /////////////////////
    std::string get_local_ip() override;
    uint16_t get_local_port() override;
    std::string get_peer_ip() override;
    uint16_t get_peer_port() override;
    const sockaddr *get_peer_addr();
    const sockaddr *get_local_addr();

    ///////////////////// TaskExecutorInterface override /////////////////////
    /**
     * Switch the task to the poller thread for execution
     * @param task The task to be executed
     * @param may_sync Whether to run the task synchronously
     */
    Task::Ptr async(TaskIn task, bool may_sync = true) override;
    Task::Ptr async_first(TaskIn task, bool may_sync = true) override;

    ///////////////////// SockSender override /////////////////////

    /**
     * Enable other non-overridden send functions in SockSender
     */
    using SockSender::send;

    /**
     * Unified data sending outlet
     */
    ssize_t send(Buffer::Ptr buf) override;
	
    /**
     * Unified export for sending data
     */
    ssize_t sendto(Buffer::Ptr buf, struct sockaddr *addr = nullptr, socklen_t addr_len = 0) override;

    /**
     * Trigger the onErr event
     */
    void shutdown(const SockException &ex = SockException(Err_shutdown, "self shutdown")) override;

    /**
     * Safely detach from the Server and trigger the onError event in a thread-safe manner
     * @param ex The reason for triggering the onError event
     */
    void safeShutdown(const SockException &ex = SockException(Err_shutdown, "self shutdown"));

    ///////////////////// event functions /////////////////////
    /**
     * Data receiving entry point
     * @param buf Data buffer, can be reused, and cannot be cached
     */
    virtual void onRecv(const Buffer::Ptr &buf) = 0;

    /**
     * Callback received eof or other events that cause disconnection from Server
     * When this event is received, the object is generally destroyed immediately
     * @param err reason
     */
    virtual void onError(const SockException &err) = 0;

    /**
     * Callback after all data has been sent
     */
    virtual void onFlush() {}

    /**
     * Triggered at regular intervals, used for timeout management
     */
    virtual void onManager() = 0;

protected:
    void setPoller(const EventPoller::Ptr &poller);
    void setSock(const Socket::Ptr &sock);

private:
    bool _try_flush = true;
    Socket::Ptr _sock;
    EventPoller::Ptr _poller;
    Socket::onCreateSocket _on_create_socket;
};

}  // namespace toolkit
#endif /* NETWORK_SOCKET_H */
