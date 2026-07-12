#include <gtest/gtest.h>

#include "Network/TcpClient.h"
#include "Network/TcpServer.h"
#include "Network/UdpClient.h"
#include "Network/UdpServer.h"
#include "Thread/semaphore.h"

using namespace toolkit;

namespace toolkit {
class TcpServerTestAccess {
public:
    static void exercise(const std::shared_ptr<TcpServer> &server, const Session::Ptr &session) {
        auto helper = std::make_shared<SessionHelper>(server, session, "synthetic-tcp-session");
        server->_session_map.emplace(helper.get(), helper);
        server->_poller->sync([&]() { server->onManagerSession(); });
        EXPECT_FALSE(server->_is_on_manager);

        auto child = std::make_shared<TcpServer>(server->_poller);
        server->_cloned_server[server->_poller.get()] = child;
        bool created = false;
        server->setOnCreateSocket([&](const EventPoller::Ptr &poller) {
            created = true;
            return Socket::createSocket(poller);
        });
        EXPECT_TRUE(server->createSocket(server->_poller));
        EXPECT_TRUE(child->createSocket(child->_poller));
        EXPECT_TRUE(created);
        EXPECT_EQ(child, server->getServer(server->_poller.get()));
        child->_parent = server;
        EXPECT_EQ(child, child->getServer(server->_poller.get()));
        server->_cloned_server.clear();
    }

    static void cloneEmptyThrows(const std::shared_ptr<TcpServer> &target, const TcpServer &source) {
        EXPECT_THROW(target->cloneFrom(source), std::invalid_argument);
    }
};

class UdpServerTestAccess {
public:
    static bool emitFirstSessionError(const std::shared_ptr<UdpServer> &server) {
        std::lock_guard<std::recursive_mutex> lock(*server->_session_mutex);
        if (server->_session_map->empty()) return false;
        return server->_session_map->begin()->second->session()->getSock()->emitErr(
            SockException(Err_other, "synthetic peer error"));
    }

    static void exerciseCrossPoller(const std::shared_ptr<UdpServer> &server,
                                    const Session::Ptr &session) {
        UdpServer::PeerIdType id;
        id[0] = 9;
        auto helper = std::make_shared<SessionHelper>(server, session, "cross-poller-session");
        {
            std::lock_guard<std::recursive_mutex> lock(*server->_session_mutex);
            server->_session_map->emplace(id, helper);
        }
        Buffer::Ptr buffer = std::make_shared<BufferString>("cross-poller");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(9000);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        server->_poller->sync([&]() {
            server->onRead_l(true, id, buffer,
                reinterpret_cast<sockaddr *>(&address), sizeof(address));
        });
        session->getPoller()->sync([]() {});
    }

    static void exercise(const std::shared_ptr<UdpServer> &server, const Session::Ptr &session) {
        server->_session_mutex = std::make_shared<std::recursive_mutex>();
        server->_session_map = std::make_shared<UdpServer::SessionMapType>();
        server->_multi_poller = false;
        UdpServer::PeerIdType id;
        auto helper = std::make_shared<SessionHelper>(server, session, "synthetic-session");
        server->_session_map->emplace(id, helper);
        Buffer::Ptr buffer = std::make_shared<BufferString>("synthetic");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(1234);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        server->_poller->sync([&]() {
            server->onManagerSession();
            helper->enable = false;
            server->onRead_l(true, id, buffer, reinterpret_cast<sockaddr *>(&address), sizeof(address));
            helper->enable = true;
            server->onRead_l(false, id, buffer, reinterpret_cast<sockaddr *>(&address), sizeof(address));
        });

        server->_on_create_socket = [](const EventPoller::Ptr &, const Buffer::Ptr &, sockaddr *, int) {
            return Socket::Ptr();
        };
        bool is_new = false;
        UdpServer::PeerIdType missing;
        missing[0] = 1;
        EXPECT_FALSE(server->getOrCreateSession(missing, buffer,
            reinterpret_cast<sockaddr *>(&address), sizeof(address), is_new));
        EXPECT_TRUE(is_new);

        sockaddr_storage invalid{};
        invalid.ss_family = AF_UNSPEC;
        EXPECT_THROW(server->onRead(buffer, reinterpret_cast<sockaddr *>(&invalid), sizeof(invalid)), std::invalid_argument);

        sockaddr_in6 ipv6{};
        ipv6.sin6_family = AF_INET6;
        ipv6.sin6_port = htons(4321);
        ipv6.sin6_addr = in6addr_loopback;
        Buffer::Ptr ipv6_buffer = std::make_shared<BufferString>("ipv6");
        EXPECT_NO_THROW(server->onRead(ipv6_buffer, reinterpret_cast<sockaddr *>(&ipv6), sizeof(ipv6)));

        server->_multi_poller = true;
        server->onManagerSession();
        server->_multi_poller = false;

        auto child = std::make_shared<UdpServer>(server->_poller);
        server->_cloned_server[server->_poller.get()] = child;
        bool child_factory_called = false;
        server->setOnCreateSocket([&](const EventPoller::Ptr &poller, const Buffer::Ptr &, sockaddr *, int) {
            child_factory_called = true;
            return Socket::createSocket(poller);
        });
        EXPECT_TRUE(child->createSocket(child->_poller, buffer,
            reinterpret_cast<sockaddr *>(&address), sizeof(address)));
        EXPECT_TRUE(child_factory_called);
        server->_cloned_server.clear();
    }

    static void cloneEmptyThrows(const std::shared_ptr<UdpServer> &target, const UdpServer &source) {
        EXPECT_THROW(target->cloneFrom(source), std::invalid_argument);
    }
};
} // namespace toolkit

namespace {
class EchoSessionForTest : public Session {
public:
    explicit EchoSessionForTest(const Socket::Ptr &socket) : Session(socket) {}
    void onRecv(const Buffer::Ptr &buffer) override { send(buffer); }
    void onError(const SockException &) override { ++errors; }
    void onManager() override { ++managed; }
    static std::atomic<int> errors;
    static std::atomic<int> managed;
};
std::atomic<int> EchoSessionForTest::errors{0};
std::atomic<int> EchoSessionForTest::managed{0};

class ThrowingSessionForTest : public Session {
public:
    explicit ThrowingSessionForTest(const Socket::Ptr &socket) : Session(socket) {}
    void onRecv(const Buffer::Ptr &) override { throw std::runtime_error("recv failure"); }
    void onError(const SockException &) override {}
    void onManager() override { throw std::runtime_error("manager failure"); }
};

class TcpClientForTest : public TcpClient {
public:
    TcpClientForTest(const EventPoller::Ptr &poller, semaphore &connected, semaphore &received)
        : TcpClient(poller), connected_sem(connected), received_sem(received) {}
    void onConnect(const SockException &error) override { connect_error = error; connected_sem.post(); }
    void onRecv(const Buffer::Ptr &buffer) override {
        if (throw_on_recv) throw std::runtime_error("tcp receive failure");
        payload = buffer->toString();
        received_sem.post();
    }
    void onError(const SockException &error) override { last_error = error; }
    void onFlush() override { ++flushes; }
    semaphore &connected_sem;
    semaphore &received_sem;
    SockException connect_error;
    SockException last_error;
    std::string payload;
    int flushes = 0;
    bool throw_on_recv = false;
};
}

TEST(TcpClientServerTest, ConnectsEchoesReportsStatsAndShutsDown) {
    auto poller = EventPollerPool::Instance().getPoller();
    auto server = std::make_shared<TcpServer>(poller);
    try {
        server->start<EchoSessionForTest>(0, "127.0.0.1", 16);
    } catch (const std::exception &error) {
        if (errno == EPERM || errno == EACCES) GTEST_SKIP() << error.what();
        throw;
    }
    ASSERT_GT(server->getPort(), 0);
    server->setOnCreateSocket([](const EventPoller::Ptr &socket_poller) { return Socket::createSocket(socket_poller); });

    semaphore connected;
    semaphore received;
    auto client = std::make_shared<TcpClientForTest>(poller, connected, received);
    EXPECT_FALSE(client->alive());
    client->setNetAdapter("127.0.0.1");
    client->startConnect("127.0.0.1", server->getPort(), 1.0f);
    ASSERT_TRUE(connected.wait(2000));
    ASSERT_FALSE(static_cast<bool>(client->connect_error));
    EXPECT_TRUE(client->alive());
    EXPECT_FALSE(client->getIdentifier().empty());
    EXPECT_EQ(0u, client->getSendTotalBytes());
    EXPECT_EQ(0u, client->getRecvTotalBytes());
    EXPECT_GT(client->send("high-level-echo"), 0);
    ASSERT_TRUE(received.wait(2000));
    EXPECT_EQ("high-level-echo", client->payload);
    EXPECT_GE(client->getSendTotalBytes(), std::string("high-level-echo").size());
    EXPECT_GE(client->getRecvTotalBytes(), std::string("high-level-echo").size());
    client->shutdown();
    for (int i = 0; i < 100 && client->alive(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_FALSE(client->alive());
    client.reset();
    server.reset();
}

TEST(TcpClientTest, ReportsConnectionFailureAndRepeatedShutdown) {
    auto poller = EventPollerPool::Instance().getPoller();
    semaphore connected;
    semaphore received;
    auto client = std::make_shared<TcpClientForTest>(poller, connected, received);
    client->startConnect("127.0.0.1", 1, 0.05f);
    ASSERT_TRUE(connected.wait(2000));
    EXPECT_TRUE(static_cast<bool>(client->connect_error));
    EXPECT_FALSE(client->alive());
    EXPECT_EQ(0u, client->getSendSpeed());
    EXPECT_EQ(0u, client->getRecvSpeed());
    EXPECT_EQ(0u, client->getSendTotalBytes());
    EXPECT_EQ(0u, client->getRecvTotalBytes());
    client->shutdown();
    client->shutdown(SockException(Err_other, "again"));
}

TEST(TcpClientTest, ConvertsReceiveExceptionsIntoShutdown) {
    auto poller = EventPollerPool::Instance().getPoller();
    auto server = std::make_shared<TcpServer>(poller);
    try {
        server->start<EchoSessionForTest>(0, "127.0.0.1", 8);
    } catch (const std::exception &error) {
        if (errno == EPERM || errno == EACCES) GTEST_SKIP() << error.what();
        throw;
    }
    semaphore connected;
    semaphore unused;
    auto client = std::make_shared<TcpClientForTest>(poller, connected, unused);
    client->throw_on_recv = true;
    client->startConnect("127.0.0.1", server->getPort(), 1.0f);
    ASSERT_TRUE(connected.wait(2000));
    ASSERT_FALSE(static_cast<bool>(client->connect_error));
    ASSERT_GT(client->send("trigger-throw"), 0);
    for (int i = 0; i < 200 && client->alive(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_FALSE(client->alive());
    client.reset();
    server.reset();
}

TEST(ServerTest, SessionMapAndHelperManageLifecycle) {
    auto socket = Socket::createSocket();
    auto session = std::make_shared<EchoSessionForTest>(socket);
    auto &map = SessionMap::Instance();
    EXPECT_FALSE(map.get("unit-session-map-missing"));

    auto server = std::make_shared<Server>();
    {
        SessionHelper helper(server, session, "EchoSessionForTest");
        EXPECT_EQ(session, helper.session());
        EXPECT_EQ("EchoSessionForTest", helper.className());
        EXPECT_EQ(session, map.get(session->getIdentifier()));
        size_t visited = 0;
        map.for_each_session([&](const std::string &key, const Session::Ptr &value) {
            if (key == session->getIdentifier()) {
                EXPECT_EQ(session, value);
                ++visited;
            }
        });
        EXPECT_EQ(1u, visited);
    }
    EXPECT_FALSE(map.get(session->getIdentifier()));

    const int errors_before = EchoSessionForTest::errors.load();
    {
        SessionHelper helper(std::weak_ptr<Server>(), session, "detached");
    }
    EXPECT_EQ(errors_before + 1, EchoSessionForTest::errors.load());
}

TEST(TcpServerTest, ExercisesManagerFactoryDispatchAndInvalidClone) {
    auto poller = EventPollerPool::Instance().getPoller();
    auto server = std::make_shared<TcpServer>(poller);
    auto throwing = std::make_shared<ThrowingSessionForTest>(Socket::createSocket(poller));
    TcpServerTestAccess::exercise(server, throwing);

    auto empty = std::make_shared<TcpServer>(poller);
    TcpServerTestAccess::cloneEmptyThrows(server, *empty);
    EXPECT_EQ(0, empty->getPort());
}

TEST(UdpClientServerTest, CreatesSessionAndEchoesDatagrams) {
    auto poller = EventPollerPool::Instance().getPoller();
    auto server = std::make_shared<UdpServer>(poller);
    try {
        server->start<EchoSessionForTest>(0, "127.0.0.1");
    } catch (const std::exception &error) {
        if (errno == EPERM || errno == EACCES) GTEST_SKIP() << error.what();
        throw;
    }
    ASSERT_GT(server->getPort(), 0);
    server->setOnCreateSocket([](const EventPoller::Ptr &socket_poller, const Buffer::Ptr &, sockaddr *, int) {
        return Socket::createSocket(socket_poller);
    });

    auto client = std::make_shared<UdpClient>(poller);
    semaphore received;
    std::string payload;
    client->setOnRecvFrom([&](const Buffer::Ptr &buffer, sockaddr *address, int) {
        payload = buffer->toString();
        EXPECT_EQ("127.0.0.1", SockUtil::inet_ntoa(address));
        received.post();
    });
    SockException client_error;
    client->setOnError([&](const SockException &error) { client_error = error; });
    client->setNetAdapter("127.0.0.1");
    client->startConnect("127.0.0.1", server->getPort(), 0);
    EXPECT_TRUE(client->alive());
    EXPECT_FALSE(client->getIdentifier().empty());
    EXPECT_GT(client->send("udp-high-level"), 0);
    ASSERT_TRUE(received.wait(2000));
    EXPECT_EQ("udp-high-level", payload);
    EXPECT_GT(client->send("udp-peer-socket"), 0);
    ASSERT_TRUE(received.wait(2000));
    EXPECT_EQ("udp-peer-socket", payload);
    const int errors_before = EchoSessionForTest::errors.load();
    ASSERT_TRUE(UdpServerTestAccess::emitFirstSessionError(server));
    for (int i = 0; i < 200 && EchoSessionForTest::errors.load() == errors_before; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_GT(EchoSessionForTest::errors.load(), errors_before);
    client->shutdown();
    EXPECT_FALSE(client->alive());
    client.reset();
    server.reset();
}

TEST(UdpClientTest, ConvertsReceiveExceptionsIntoShutdown) {
    auto poller = EventPollerPool::Instance().getPoller();
    auto server = std::make_shared<UdpServer>(poller);
    try {
        server->start<EchoSessionForTest>(0, "127.0.0.1");
    } catch (const std::exception &error) {
        if (errno == EPERM || errno == EACCES) GTEST_SKIP() << error.what();
        throw;
    }
    auto client = std::make_shared<UdpClient>(poller);
    semaphore errored;
    client->setOnRecvFrom([](const Buffer::Ptr &, sockaddr *, int) {
        throw std::runtime_error("udp receive failure");
    });
    client->setOnError([&](const SockException &) { errored.post(); });
    client->startConnect("127.0.0.1", server->getPort());
    ASSERT_GT(client->send("trigger-throw"), 0);
    ASSERT_TRUE(errored.wait(2000));
    EXPECT_FALSE(client->alive());
    client.reset();
    server.reset();
}

TEST(TcpClientServerTest, MultiPollerServerClonesAndEchoes) {
    auto server = std::make_shared<TcpServer>();
    try {
        server->start<EchoSessionForTest>(0, "127.0.0.1", 16);
    } catch (const std::exception &error) {
        if (errno == EPERM || errno == EACCES) GTEST_SKIP() << error.what();
        throw;
    }
    ASSERT_GT(server->getPort(), 0);
    semaphore connected;
    semaphore received;
    auto client = std::make_shared<TcpClientForTest>(EventPollerPool::Instance().getPoller(), connected, received);
    client->startConnect("127.0.0.1", server->getPort(), 1.0f);
    ASSERT_TRUE(connected.wait(2000));
    ASSERT_FALSE(static_cast<bool>(client->connect_error));
    ASSERT_GT(client->send("multi-poller-tcp"), 0);
    ASSERT_TRUE(received.wait(2000));
    EXPECT_EQ("multi-poller-tcp", client->payload);
    client->shutdown();
    client.reset();
    server.reset();
}

TEST(UdpClientServerTest, MultiPollerServerHandlesTwoPeers) {
    auto server = std::make_shared<UdpServer>();
    try {
        server->start<EchoSessionForTest>(0, "127.0.0.1");
    } catch (const std::exception &error) {
        if (errno == EPERM || errno == EACCES) GTEST_SKIP() << error.what();
        throw;
    }
    ASSERT_GT(server->getPort(), 0);
    auto poller = EventPollerPool::Instance().getPoller();
    auto first = std::make_shared<UdpClient>(poller);
    auto second = std::make_shared<UdpClient>(poller);
    semaphore first_received;
    semaphore second_received;
    std::string first_payload;
    std::string second_payload;
    first->setOnRecvFrom([&](const Buffer::Ptr &buf, sockaddr *, int) {
        first_payload = buf->toString();
        first_received.post();
    });
    second->setOnRecvFrom([&](const Buffer::Ptr &buf, sockaddr *, int) {
        second_payload = buf->toString();
        second_received.post();
    });
    first->startConnect("127.0.0.1", server->getPort());
    second->startConnect("127.0.0.1", server->getPort());
    ASSERT_GT(first->send("peer-one"), 0);
    ASSERT_GT(second->send("peer-two"), 0);
    ASSERT_TRUE(first_received.wait(2000));
    ASSERT_TRUE(second_received.wait(2000));
    EXPECT_EQ("peer-one", first_payload);
    EXPECT_EQ("peer-two", second_payload);
    first->shutdown();
    second->shutdown();
    first.reset();
    second.reset();
    server.reset();
}

TEST(UdpServerTest, InternalDispatchHandlesDisabledExistingMissingAndInvalidPeers) {
    auto poller = EventPollerPool::Instance().getPoller();
    auto server = std::make_shared<UdpServer>(poller);
    auto session = std::make_shared<EchoSessionForTest>(Socket::createSocket(poller));
    UdpServerTestAccess::exercise(server, session);
    auto empty = std::make_shared<UdpServer>(poller);
    UdpServerTestAccess::cloneEmptyThrows(server, *empty);
    EXPECT_EQ(0, empty->getPort());
    EXPECT_NO_THROW(empty->joinMultiAddr("239.1.1.1"));
    EXPECT_NO_THROW(empty->leaveMultiAddr("239.1.1.1"));

    auto throwing_server = std::make_shared<UdpServer>(poller);
    auto throwing = std::make_shared<ThrowingSessionForTest>(Socket::createSocket(poller));
    UdpServerTestAccess::exercise(throwing_server, throwing);

    EventPoller::Ptr other;
    EventPollerPool::Instance().for_each([&](const TaskExecutor::Ptr &executor) {
        auto candidate = std::static_pointer_cast<EventPoller>(executor);
        if (!other && candidate != poller) other = candidate;
    });
    ASSERT_TRUE(other);
    ASSERT_NE(other, poller);
    auto cross = std::make_shared<ThrowingSessionForTest>(Socket::createSocket(other));
    UdpServerTestAccess::exerciseCrossPoller(server, cross);
}
