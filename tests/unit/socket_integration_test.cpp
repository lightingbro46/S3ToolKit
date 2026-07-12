#include <gtest/gtest.h>

#include "Network/Socket.h"
#include "Network/BufferSock.h"
#include "Thread/semaphore.h"

using namespace toolkit;

namespace toolkit {
class SocketTestAccess {
    class ScriptedRecvBuffer : public SocketRecvBuffer {
    public:
        explicit ScriptedRecvBuffer(std::vector<ssize_t> values) : values(std::move(values)) {
            buffer = std::make_shared<BufferString>("abc");
        }
        ssize_t recvFromSocket(int, ssize_t &count) override {
            if (position >= values.size()) {
                errno = EAGAIN;
                return -1;
            }
            auto value = values[position++];
            count = value > 0 ? 1 : 0;
            if (value < 0) errno = EAGAIN;
            return value;
        }
        Buffer::Ptr &getBuffer(size_t) override { return buffer; }
        sockaddr_storage &getAddress(size_t) override { return address; }
        std::vector<ssize_t> values;
        size_t position = 0;
        Buffer::Ptr buffer;
        sockaddr_storage address{};
    };

public:
    static void exerciseDefaults(const Socket::Ptr &socket) {
        Buffer::Ptr buffer = std::make_shared<BufferString>("ignored");
        sockaddr_storage address{};
        socket->_on_multi_read(&buffer, &address, 1);
        auto dummy = Socket::createSocket(socket->_poller);
        std::shared_ptr<void> complete;
        socket->_on_accept(dummy, complete);
        EXPECT_FALSE(socket->_on_before_accept(socket->_poller));
        EXPECT_TRUE(socket->_on_flush());
    }

    static void exerciseReadStates(const Socket::Ptr &socket) {
        auto udp = std::make_shared<SockNum>(-1, SockNum::Sock_UDP);
        auto tcp = std::make_shared<SockNum>(-1, SockNum::Sock_TCP);
        socket->setOnRead([](Buffer::Ptr &, sockaddr *, int) { throw std::runtime_error("read callback"); });
        auto data_then_again = std::make_shared<ScriptedRecvBuffer>(std::vector<ssize_t>{3, -1});
        EXPECT_EQ(3, socket->onRead(udp, data_then_again));
        auto udp_eof = std::make_shared<ScriptedRecvBuffer>(std::vector<ssize_t>{0});
        EXPECT_EQ(0, socket->onRead(udp, udp_eof));
        auto tcp_error = std::make_shared<ScriptedRecvBuffer>(std::vector<ssize_t>{-1});
        EXPECT_EQ(0, socket->onRead(tcp, tcp_error));
    }

    static void exerciseFlushStates(const Socket::Ptr &socket) {
        int fds[2] = {-1, -1};
        ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
        auto number = std::make_shared<SockNum>(fds[0], SockNum::Sock_TCP);
        socket->setSock(number);
        socket->getSendTotalBytes();
        bool callback = false;
        socket->setOnSendResult([&](const Buffer::Ptr &, bool success) { callback = success; });
        EXPECT_EQ(5, socket->send("flush", 5, nullptr, 0, false));
        EXPECT_TRUE(socket->flushData(number, false));
        EXPECT_TRUE(callback);
        char data[8] = {0};
        EXPECT_EQ(5, ::recv(fds[1], data, sizeof(data), 0));
        socket->onWriteAble(number);
        socket->setOnFlush([]() { return false; });
        socket->onFlushed();
        socket->onFlushed();

        socket->_sendable = false;
        socket->_max_send_buffer_ms = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // An already-drained queue returns 0; a pending queue past the configured
        // deadline returns -1. Both are valid after the synchronous flush above.
        EXPECT_LE(socket->flushAll(), 0);
        socket->_sendable = true;
        ::close(fds[1]);
    }
};
} // namespace toolkit

namespace {
class SocketHelperForTest : public SocketHelper {
public:
    explicit SocketHelperForTest(const Socket::Ptr &socket) : SocketHelper(socket) {}
    void onRecv(const Buffer::Ptr &) override {}
    void onError(const SockException &) override {}
    void onManager() override {}
    using SocketHelper::createSocket;
    using SocketHelper::setPoller;
    using SocketHelper::setSock;
};
}

TEST(SocketTest, ReportsExceptionsAndBasicState) {
    SockException error(Err_timeout, "timed out", 123);
    EXPECT_TRUE(static_cast<bool>(error));
    EXPECT_EQ(Err_timeout, error.getErrCode());
    EXPECT_EQ(123, error.getCustomCode());
    EXPECT_STREQ("timed out", error.what());
    error.reset(Err_success, "ok");
    EXPECT_FALSE(static_cast<bool>(error));

    auto socket = Socket::createSocket();
    EXPECT_FALSE(socket->alive());
    EXPECT_EQ(-1, socket->rawFD());
    EXPECT_EQ(-1, socket->send("not-connected"));
    EXPECT_FALSE(socket->isSocketBusy());
    EXPECT_EQ(SockNum::Sock_Invalid, socket->sockType());
    EXPECT_EQ(1u, socket->getSendBufferCount());
    EXPECT_EQ(0u, socket->getRecvSpeed());
    EXPECT_EQ(0u, socket->getSendSpeed());
    EXPECT_EQ(0u, socket->getRecvTotalBytes());
    EXPECT_EQ(0u, socket->getSendTotalBytes());
    EXPECT_EQ("", socket->get_local_ip());
    EXPECT_EQ(0, socket->get_local_port());
    EXPECT_EQ("", socket->get_peer_ip());
    EXPECT_EQ(0, socket->get_peer_port());
    EXPECT_TRUE(socket->get_local_addr());
    EXPECT_TRUE(socket->get_peer_addr());
    EXPECT_EQ(0, socket->send(""));
    EXPECT_EQ(0, socket->send(std::make_shared<BufferString>("")));
    EXPECT_EQ(-1, socket->flushAll());
    EXPECT_FALSE(socket->bindPeerAddr(nullptr));
    socket->setSendFlags(0);
    socket->setSendTimeOutSecond(1);
    socket->enableRecv(false);
    socket->enableRecv(true);
    bool errored = false;
    socket->setOnErr([&](const SockException &reported) { errored = static_cast<bool>(reported); });
    EXPECT_TRUE(socket->emitErr(SockException(Err_other, "synthetic")));
    EXPECT_TRUE(socket->emitErr(SockException(Err_other, "duplicate")));
    for (int i = 0; i < 100 && !errored; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_TRUE(errored);
    socket->closeSock();
}

TEST(SocketTest, InternalDefaultReadFlushAndTimeoutStates) {
    auto socket = Socket::createSocket();
    SocketTestAccess::exerciseDefaults(socket);
    SocketTestAccess::exerciseReadStates(socket);
    socket = Socket::createSocket();
    SocketTestAccess::exerciseFlushStates(socket);
}

TEST(SocketTest, ReportsInvalidHostConnectionFailure) {
    auto socket = Socket::createSocket();
    semaphore completed;
    SockException result;
    socket->connect("256.256.256.256", 1234, [&](const SockException &error) {
        result = error;
        completed.post();
    }, 0.1f, "127.0.0.1");
    ASSERT_TRUE(completed.wait(2000));
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_FALSE(socket->alive());
}

TEST(SocketHelperTest, HandlesMissingAndAttachedSockets) {
    auto poller = EventPollerPool::Instance().getPoller();
    auto helper = std::make_shared<SocketHelperForTest>(nullptr);
    helper->setPoller(poller);
    EXPECT_EQ(poller, helper->getPoller());
    EXPECT_FALSE(helper->getSock());
    EXPECT_EQ(-1, helper->flushAll());
    EXPECT_EQ(-1, helper->send(std::make_shared<BufferString>("x")));
    EXPECT_EQ(-1, helper->sendto(std::make_shared<BufferString>("x")));
    (*helper) << "char-data" << std::string("string-data")
              << std::static_pointer_cast<Buffer>(std::make_shared<BufferString>("buffer-data"));
    EXPECT_EQ("", helper->get_local_ip());
    EXPECT_EQ(0, helper->get_local_port());
    EXPECT_EQ("", helper->get_peer_ip());
    EXPECT_EQ(0, helper->get_peer_port());
    EXPECT_EQ(nullptr, helper->get_local_addr());
    EXPECT_EQ(nullptr, helper->get_peer_addr());
    EXPECT_TRUE(helper->isSocketBusy());
    helper->setSendFlags(0);
    helper->setSendFlushFlag(false);
    helper->setOnCreateSocket(nullptr);
    EXPECT_TRUE(helper->createSocket());
    helper->setOnCreateSocket([](const EventPoller::Ptr &p) { return Socket::createSocket(p); });
    EXPECT_TRUE(helper->createSocket());

    auto socket = Socket::createSocket(poller);
    helper->setSock(socket);
    EXPECT_EQ(socket, helper->getSock());
    EXPECT_TRUE(helper->getIdentifier().empty());
    EXPECT_FALSE(helper->isSocketBusy());
    helper->setSendFlags(0);
    helper->shutdown();
    semaphore async_done;
    helper->async([&]() { async_done.post(); });
    ASSERT_TRUE(async_done.wait(1000));
    semaphore first_done;
    helper->async_first([&]() { first_done.post(); });
    ASSERT_TRUE(first_done.wait(1000));
}

TEST(BufferSockTest, SendsTcpListsAndReportsSuccessAndFailure) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    List<std::pair<Buffer::Ptr, bool>> packets;
    packets.emplace_back(std::make_shared<BufferString>("one"), false);
    packets.emplace_back(std::make_shared<BufferString>("two"), false);
    std::vector<bool> results;
    auto list = BufferList::create(std::move(packets), [&](const Buffer::Ptr &, bool success) {
        results.push_back(success);
    }, false);
    EXPECT_FALSE(list->empty());
    EXPECT_EQ(2u, list->count());
    EXPECT_EQ(6, list->send(fds[0], 0));
    EXPECT_TRUE(list->empty());
    EXPECT_EQ(2u, list->count());
    char received[8] = {0};
    EXPECT_EQ(6, ::recv(fds[1], received, sizeof(received), 0));
    EXPECT_EQ("onetwo", std::string(received, 6));
    EXPECT_EQ(std::vector<bool>({true, true}), results);

    List<std::pair<Buffer::Ptr, bool>> failed_packets;
    failed_packets.emplace_back(std::make_shared<BufferString>("fail"), false);
    bool failed = false;
    {
        auto failed_list = BufferList::create(std::move(failed_packets),
            [&](const Buffer::Ptr &, bool success) { failed = !success; }, false);
        EXPECT_EQ(-1, failed_list->send(-1, 0));
        EXPECT_FALSE(failed_list->empty());
    }
    EXPECT_TRUE(failed);
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(BufferSockTest, ResumesPartiallyWrittenTcpVector) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    ASSERT_EQ(0, SockUtil::setNoBlocked(fds[0]));
    ASSERT_EQ(0, SockUtil::setNoBlocked(fds[1]));

    List<std::pair<Buffer::Ptr, bool>> packets;
    for (char fill : {'a', 'b', 'c'}) {
        packets.emplace_back(std::make_shared<BufferString>(std::string(128 * 1024, fill)), false);
    }
    size_t completed = 0;
    auto list = BufferList::create(std::move(packets),
        [&](const Buffer::Ptr &, bool success) { if (success) ++completed; }, false);
    const auto first = list->send(fds[0], 0);
    ASSERT_GT(first, 0);
    EXPECT_FALSE(list->empty());

    std::array<char, 64 * 1024> drain{};
    for (int attempt = 0; attempt < 200 && !list->empty(); ++attempt) {
        while (::recv(fds[1], drain.data(), drain.size(), 0) > 0) {}
        list->send(fds[0], 0);
    }
    while (::recv(fds[1], drain.data(), drain.size(), 0) > 0) {}
    EXPECT_TRUE(list->empty());
    EXPECT_EQ(3u, completed);
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(BufferSockTest, ReceivesTcpAndUdpBuffersAndPreservesAddresses) {
    int pair[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_DGRAM, 0, pair));
    auto tcp_buffer = SocketRecvBuffer::create(false);
    const int pair_send = static_cast<int>(::send(pair[0], "data", 4, 0));
    if (pair_send < 0 && (errno == EPERM || errno == EACCES)) {
        ::close(pair[0]);
        ::close(pair[1]);
        GTEST_SKIP() << "datagram send prohibited";
    }
    ASSERT_EQ(4, pair_send);
    ssize_t count = 0;
    EXPECT_EQ(4, tcp_buffer->recvFromSocket(pair[1], count));
    EXPECT_EQ(1, count);
    EXPECT_EQ("data", tcp_buffer->getBuffer(0)->toString());
    EXPECT_NO_THROW(tcp_buffer->getAddress(0));
    ::close(pair[0]);
    ::close(pair[1]);

    int receiver = SockUtil::bindUdpSock(0, "127.0.0.1", true);
    if (receiver < 0 && (errno == EPERM || errno == EACCES)) GTEST_SKIP() << "UDP sockets prohibited";
    ASSERT_GE(receiver, 0);
    int sender = SockUtil::bindUdpSock(0, "127.0.0.1", true);
    ASSERT_GE(sender, 0);
    auto destination = SockUtil::make_sockaddr("127.0.0.1", SockUtil::get_local_port(receiver));
    auto wrapped = std::make_shared<BufferSock>(std::make_shared<BufferString>("udp-list"),
        reinterpret_cast<sockaddr *>(&destination));
    EXPECT_EQ("udp-list", wrapped->toString());
    EXPECT_EQ(sizeof(sockaddr_in), wrapped->socklen());
    EXPECT_EQ("127.0.0.1", SockUtil::inet_ntoa(wrapped->sockaddr()));
    List<std::pair<Buffer::Ptr, bool>> udp_packets;
    udp_packets.emplace_back(wrapped, true);
    bool sent = false;
    auto udp_list = BufferList::create(std::move(udp_packets),
        [&](const Buffer::Ptr &, bool success) { sent = success; }, true);
    EXPECT_EQ(1u, udp_list->count());
    EXPECT_GT(udp_list->send(sender, 0), 0);
    EXPECT_TRUE(sent);
    auto udp_buffer = SocketRecvBuffer::create(true);
    count = 0;
    EXPECT_EQ(8, udp_buffer->recvFromSocket(receiver, count));
    EXPECT_EQ(1, count);
    EXPECT_EQ("udp-list", udp_buffer->getBuffer(0)->toString());
    EXPECT_EQ("127.0.0.1", SockUtil::inet_ntoa(reinterpret_cast<sockaddr *>(&udp_buffer->getAddress(0))));
    ::close(sender);
    ::close(receiver);
}

TEST(SocketTest, AdoptsMovesAndClonesRawDescriptors) {
    int pair_fd[2] = {-1, -1};
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, pair_fd));
    auto first_poller = EventPollerPool::Instance().getPoller();
    auto second_poller = EventPollerPool::Instance().getPoller();
    auto socket = Socket::createSocket(first_poller);
    ASSERT_TRUE(socket->fromSock(pair_fd[0], SockNum::Sock_TCP));
    EXPECT_TRUE(socket->alive());
    EXPECT_EQ(pair_fd[0], socket->rawFD());
    socket->moveTo(second_poller);
    EXPECT_EQ(second_poller, socket->getPoller());

    auto clone = Socket::createSocket(first_poller);
    auto token = clone->cloneSocket(*socket);
    EXPECT_TRUE(token);
    EXPECT_EQ(socket->rawFD(), clone->rawFD());
    EXPECT_TRUE(clone->alive());
    clone->closeSock(false);
    socket->closeSock();
    ::close(pair_fd[1]);
}

TEST(SocketTest, ExchangesTcpDataOverLoopback) {
    auto poller = EventPollerPool::Instance().getPoller();
    auto listener = Socket::createSocket(poller);
    if (!listener->listen(0, "127.0.0.1")) {
        if (errno == EPERM || errno == EACCES) {
            GTEST_SKIP() << "socket creation is prohibited by the execution sandbox";
        }
        FAIL() << "unable to listen on loopback";
    }
    const auto port = listener->get_local_port();
    EXPECT_GT(port, 0);

    semaphore accepted_done;
    semaphore server_read;
    semaphore client_read;
    Socket::Ptr accepted;
    std::string server_payload;
    std::string client_payload;
    listener->setOnBeforeAccept([](const EventPoller::Ptr &accept_poller) {
        return Socket::createSocket(accept_poller);
    });
    listener->setOnAccept([&](Socket::Ptr &socket, std::shared_ptr<void> &) {
        accepted = socket;
        accepted->setOnRead([&](Buffer::Ptr &buffer, sockaddr *, int) {
            server_payload = buffer->toString();
            accepted->send("pong");
            server_read.post();
        });
        accepted_done.post();
    });

    auto client = Socket::createSocket(poller);
    client->setOnRead([&](Buffer::Ptr &buffer, sockaddr *, int) {
        client_payload = buffer->toString();
        client_read.post();
    });
    semaphore connected;
    SockException connect_result;
    client->connect("127.0.0.1", port, [&](const SockException &error) {
        connect_result = error;
        connected.post();
    }, 1.0f, "127.0.0.1");
    ASSERT_TRUE(connected.wait(2000));
    ASSERT_FALSE(static_cast<bool>(connect_result));
    ASSERT_TRUE(accepted_done.wait(2000));
    EXPECT_TRUE(client->alive());
    EXPECT_TRUE(accepted->alive());
    EXPECT_EQ(port, client->get_peer_port());
    EXPECT_EQ("127.0.0.1", client->get_peer_ip());
    EXPECT_FALSE(client->getIdentifier().empty());
    EXPECT_EQ(0u, client->getSendTotalBytes());
    EXPECT_EQ(0u, client->getRecvTotalBytes());
    std::atomic<int> send_results{0};
    client->setOnSendResult([&](const Buffer::Ptr &, bool success) { if (success) ++send_results; });
    EXPECT_GT(client->send("ping"), 0);
    ASSERT_TRUE(server_read.wait(2000));
    ASSERT_TRUE(client_read.wait(2000));
    EXPECT_EQ("ping", server_payload);
    EXPECT_EQ("pong", client_payload);
    EXPECT_GE(client->getSendTotalBytes(), 4u);
    EXPECT_GE(client->getRecvTotalBytes(), 4u);
    EXPECT_GE(send_results.load(), 1);
    EXPECT_TRUE(client->get_local_addr());
    EXPECT_TRUE(client->get_peer_addr());
    client->elapsedTimeAfterFlushed();
    client->setOnFlush([]() { return false; });
    client->setOnSendResult([](const Buffer::Ptr &, bool) {});
    client->flushAll();
    client->closeSock();
    accepted->closeSock();
    listener->closeSock();
}

TEST(SocketTest, ExchangesUdpDataOverLoopback) {
    auto poller = EventPollerPool::Instance().getPoller();
    auto receiver = Socket::createSocket(poller);
    if (!receiver->bindUdpSock(0, "127.0.0.1")) {
        if (errno == EPERM || errno == EACCES) {
            GTEST_SKIP() << "socket creation is prohibited by the execution sandbox";
        }
        FAIL() << "unable to bind UDP loopback";
    }
    auto sender = Socket::createSocket(poller);
    ASSERT_TRUE(sender->bindUdpSock(0, "127.0.0.1"));
    const auto destination = SockUtil::make_sockaddr("127.0.0.1", receiver->get_local_port());
    EXPECT_TRUE(sender->bindPeerAddr(reinterpret_cast<const sockaddr *>(&destination), sizeof(sockaddr_in), true));
    semaphore received;
    std::string payload;
    receiver->setOnRead([&](Buffer::Ptr &buffer, sockaddr *address, int) {
        payload = buffer->toString();
        EXPECT_EQ("127.0.0.1", SockUtil::inet_ntoa(address));
        received.post();
    });
    EXPECT_GT(sender->send("udp-message", reinterpret_cast<sockaddr *>(const_cast<sockaddr_storage *>(&destination))), 0);
    ASSERT_TRUE(received.wait(2000));
    EXPECT_EQ("udp-message", payload);
    EXPECT_FALSE(sender->bindPeerAddr(nullptr));
    sender->closeSock();
    receiver->closeSock();
}
