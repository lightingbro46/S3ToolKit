#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "Network/Kcp.h"
#include "Network/sockutil.h"
#include "Thread/semaphore.h"

using namespace toolkit;

namespace toolkit {
class KcpTransportAccess : public KcpTransport {
public:
    explicit KcpTransportAccess(bool server) : KcpTransport(server, EventPollerPool::Instance().getPoller()) {}
    using KcpTransport::decreaseCwnd;
    using KcpTransport::dropCacheByAck;
    using KcpTransport::dropCacheByUna;
    using KcpTransport::flushPool;
    using KcpTransport::getRcvWndUnused;
    using KcpTransport::getWaitSnd;
    using KcpTransport::handleAnyPacket;
    using KcpTransport::handleCmdAck;
    using KcpTransport::handleCmdPush;
    using KcpTransport::increaseCwnd;
    using KcpTransport::mergeSendQueue;
    using KcpTransport::onData;
    using KcpTransport::peeksize;
    using KcpTransport::sendAckList;
    using KcpTransport::sendProbePacket;
    using KcpTransport::sendPacket;
    using KcpTransport::sendSendQueue;
    using KcpTransport::sortRecvBuf;
    using KcpTransport::sortSendQueue;
    using KcpTransport::update;
    using KcpTransport::updateFastAck;
    using KcpTransport::updateRtt;

    void exercisePrivateStates() {
        _conv = 9;
        _conv_init = true;
        _rmt_wnd = 0;
        _probe_wait = 1;
        _ts_probe = 0;
        _probe = IKCP_ASK_TELL;
        sendProbePacket();
        _rmt_wnd = 0;
        _probe_wait = IKCP_PROBE_LIMIT;
        _ts_probe = 0;
        sendProbePacket();
        _rmt_wnd = 32;
        sendProbePacket();

        _snd_una = 0;
        _snd_nxt = 3;
        for (uint32_t sn = 0; sn < 3; ++sn) {
            auto packet = std::make_shared<KcpDataPacket>(_conv, 1);
            packet->setSn(sn);
            packet->setTs(sn);
            packet->setFastack(0);
            _snd_buf.push_back(packet);
        }
        updateFastAck(2, 10);
        _fastack_conserve = true;
        updateFastAck(2, 0);
        dropCacheByAck(1);
        dropCacheByUna(3);

        _cwnd = 1;
        _rmt_wnd = 10;
        _ssthresh = 2;
        _incr = 0;
        increaseCwnd();
        increaseCwnd();
        _cwnd = 0;
        decreaseCwnd(false, false);

        _rcv_queue.clear();
        for (uint32_t i = 0; i < _rcv_wnd; ++i) {
            _rcv_queue.push_back(std::make_shared<KcpDataPacket>(_conv, 1));
        }
        (void)getRcvWndUnused();
        _rcv_queue.clear();

        auto out_of_window = std::make_shared<KcpDataPacket>(_conv, 1);
        out_of_window->setSn(_rcv_nxt + _rcv_wnd);
        handleCmdPush(out_of_window);
        auto old = std::make_shared<KcpDataPacket>(_conv, 1);
        old->setSn(_rcv_nxt ? _rcv_nxt - 1 : 0);
        _rcv_nxt = 1;
        handleCmdPush(old);

        _snd_buf.clear();
        _snd_queue.clear();
        auto timeout_packet = std::make_shared<KcpDataPacket>(_conv, 1);
        timeout_packet->setSn(3);
        timeout_packet->setXmit(_dead_link);
        timeout_packet->setRto(10);
        timeout_packet->setResendts(0);
        _snd_buf.push_back(timeout_packet);
        _delay_mode = DELAY_MODE_FAST;
        sendSendQueue();

        _snd_buf.clear();
        auto fast_packet = std::make_shared<KcpDataPacket>(_conv, 1);
        fast_packet->setSn(4);
        fast_packet->setXmit(1);
        fast_packet->setRto(10);
        fast_packet->setResendts(std::numeric_limits<uint32_t>::max());
        fast_packet->setFastack(5);
        _fastresend = 1;
        _snd_buf.push_back(fast_packet);
        sendSendQueue();

        _snd_buf.clear();
        _snd_queue.clear();
        _snd_una = 0;
        _snd_nxt = 1;
        _cwnd = 1;
        _snd_queue.push_back(std::make_shared<KcpDataPacket>(_conv, 1));
        sortSendQueue();

        auto immediate = std::make_shared<KcpTellPacket>(_conv);
        sendPacket(immediate, true);
    }
};
} // namespace toolkit

TEST(KcpPacketTest, SerializesParsesAndRejectsMalformedPackets) {
    KcpPacket packet(0x12345678, KcpHeader::Cmd::CMD_PUSH, 4);
    packet.setFrg(2);
    packet.setWnd(32);
    packet.setTs(100);
    packet.setSn(7);
    packet.setUna(3);
    memcpy(packet.getPayloadData(), "data", 4);
    ASSERT_TRUE(packet.storeToData());

    auto parsed = KcpPacket::parse(packet.data(), packet.size());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(0x12345678u, parsed->getConv());
    EXPECT_EQ(KcpHeader::Cmd::CMD_PUSH, parsed->getCmd());
    EXPECT_EQ(2, parsed->getFrg());
    EXPECT_EQ(32, parsed->getWnd());
    EXPECT_EQ(7u, parsed->getSn());
    EXPECT_EQ("data", std::string(parsed->getPayloadData(), parsed->getLen()));
    EXPECT_FALSE(KcpPacket::parse(packet.data(), KcpHeader::HEADER_SIZE - 1));

    std::array<char, KcpHeader::HEADER_SIZE> empty_header = {{0}};
    EXPECT_TRUE(KcpPacket::parse(empty_header.data(), empty_header.size()));

    KcpHeader header;
    char too_small[KcpHeader::HEADER_SIZE - 1] = {0};
    EXPECT_FALSE(header.storeHeaderToData(too_small, sizeof(too_small)));
    KcpPacket truncated_payload(1, KcpHeader::Cmd::CMD_PUSH, 0);
    truncated_payload.setLen(100);
    ASSERT_TRUE(truncated_payload.storeToData());
    EXPECT_FALSE(KcpPacket::parse(truncated_payload.data(), truncated_payload.size()));
}

TEST(KcpTransportTest, DeliversFragmentedDataAndAcknowledgementsInMemory) {
    auto poller = EventPollerPool::Instance().getPoller();
    auto client = std::make_shared<KcpTransport>(false, poller);
    auto server = std::make_shared<KcpTransport>(true, poller);
    client->setMtu(256);
    server->setMtu(256);
    client->setInterval(5);
    server->setInterval(5);
    client->setRxMinrto(10);
    server->setRxMinrto(10);
    client->setWndSize(64, 64);
    server->setWndSize(64, 64);
    client->setDelayMode(KcpTransport::DELAY_MODE_NO_DELAY);
    server->setDelayMode(KcpTransport::DELAY_MODE_FAST);
    client->setFastResend(1);
    client->setFastackConserve(false);
    client->setNoCwnd(true);

    client->setOnWrite([server](const Buffer::Ptr &buffer) { server->input(buffer); });
    server->setOnWrite([client](const Buffer::Ptr &buffer) { client->input(buffer); });

    semaphore received;
    std::string output;
    server->setOnRead([&](const Buffer::Ptr &buffer) { output = buffer->toString(); received.post(); });
    const std::string payload(4000, 'k');
    auto input = std::make_shared<BufferString>(payload);
    EXPECT_EQ(static_cast<ssize_t>(payload.size()), client->send(input, true));
    ASSERT_TRUE(received.wait(2000));
    EXPECT_EQ(payload, output);
    client->setOnWrite(nullptr);
    server->setOnWrite(nullptr);
    server->setOnRead(nullptr);
}

TEST(KcpTransportTest, SupportsStreamModeAndEmptyInput) {
    auto poller = EventPollerPool::Instance().getPoller();
    auto client = std::make_shared<KcpTransport>(false, poller);
    auto server = std::make_shared<KcpTransport>(true, poller);
    client->setStreamMode(true);
    server->setStreamMode(true);
    client->setOnWrite([server](const Buffer::Ptr &buffer) { server->input(buffer); });
    server->setOnWrite([client](const Buffer::Ptr &buffer) { client->input(buffer); });
    EXPECT_EQ(0, client->send(std::make_shared<BufferString>(std::string()), true));

    semaphore received;
    std::string output;
    server->setOnRead([&](const Buffer::Ptr &buffer) { output += buffer->toString(); received.post(); });
    client->send(std::make_shared<BufferString>(std::string("hello")), false);
    client->send(std::make_shared<BufferString>(std::string(" world")), true);
    ASSERT_TRUE(received.wait(2000));
    EXPECT_EQ("hello world", output);
    client->setOnWrite(nullptr);
    server->setOnWrite(nullptr);
    server->setOnRead(nullptr);
}

TEST(KcpTransportTest, ValidatesServerStateSizeAndMalformedInput) {
    auto poller = EventPollerPool::Instance().getPoller();
    auto server = std::make_shared<KcpTransport>(true, poller);
    EXPECT_EQ(-1, server->send(std::make_shared<BufferString>(std::string("before-conv")), true));
    const std::string oversized(KcpTransport::IKCP_MTU_DEF * KcpTransport::IKCP_WND_RCV, 'x');
    auto client = std::make_shared<KcpTransport>(false, poller);
    EXPECT_EQ(-1, client->send(std::make_shared<BufferString>(oversized), true));
    server->input(std::make_shared<BufferString>(std::string("short")));
    server->setMtu(64);
    server->setInterval(1);
    server->setRxMinrto(1);
    server->setWndSize(1, 1);
    server->setDelayMode(KcpTransport::DELAY_MODE_NORMAL);
    server->setFastResend(0);
    server->setFastackConserve(true);
    server->setNoCwnd(false);
    server->setStreamMode(false);
}

TEST(KcpTransportTest, RetransmitsDroppedPackets) {
    auto poller = EventPollerPool::Instance().getPoller();
    auto client = std::make_shared<KcpTransport>(false, poller);
    auto server = std::make_shared<KcpTransport>(true, poller);
    client->setInterval(5);
    server->setInterval(5);
    client->setRxMinrto(10);
    client->setDelayMode(KcpTransport::DELAY_MODE_NO_DELAY);
    client->setNoCwnd(true);
    std::atomic<int> transmissions{0};
    client->setOnWrite([server, &transmissions](const Buffer::Ptr &buffer) {
        if (++transmissions > 1) server->input(buffer);
    });
    server->setOnWrite([client](const Buffer::Ptr &buffer) { client->input(buffer); });
    semaphore received;
    std::string payload;
    server->setOnRead([&](const Buffer::Ptr &buffer) { payload = buffer->toString(); received.post(); });
    client->send(std::make_shared<BufferString>(std::string("retransmit-me")), true);
    ASSERT_TRUE(received.wait(2000));
    EXPECT_EQ("retransmit-me", payload);
    EXPECT_GT(transmissions.load(), 1);
    client->setOnWrite(nullptr);
    server->setOnWrite(nullptr);
    server->setOnRead(nullptr);
}

TEST(KcpTransportTest, InternalAlgorithmsHandleEmptyAndSyntheticState) {
    auto transport = std::make_shared<KcpTransportAccess>(false);
    EXPECT_EQ(0, transport->peeksize());
    EXPECT_EQ(0, transport->getWaitSnd());
    EXPECT_GT(transport->getRcvWndUnused(), 0);
    EXPECT_NO_THROW(transport->onData());
    EXPECT_NO_THROW(transport->sortRecvBuf());
    EXPECT_NO_THROW(transport->sortSendQueue());
    EXPECT_EQ(0u, transport->mergeSendQueue("abc", 3));
    EXPECT_NO_THROW(transport->dropCacheByUna(100));
    EXPECT_NO_THROW(transport->dropCacheByAck(100));
    EXPECT_NO_THROW(transport->updateFastAck(100, 1));
    EXPECT_NO_THROW(transport->updateRtt(50));
    EXPECT_NO_THROW(transport->updateRtt(100));
    EXPECT_NO_THROW(transport->updateRtt(-1));
    EXPECT_NO_THROW(transport->increaseCwnd());
    EXPECT_NO_THROW(transport->decreaseCwnd(false, false));
    EXPECT_NO_THROW(transport->decreaseCwnd(true, false));
    EXPECT_NO_THROW(transport->decreaseCwnd(false, true));
    EXPECT_NO_THROW(transport->sendAckList());
    EXPECT_NO_THROW(transport->sendProbePacket());
    EXPECT_NO_THROW(transport->sendSendQueue());
    EXPECT_NO_THROW(transport->flushPool());
    EXPECT_NO_THROW(transport->update());

    auto ack = std::make_shared<KcpAckPacket>(1);
    ack->setSn(0);
    ack->setTs(1);
    ack->setUna(0);
    ack->setWnd(32);
    EXPECT_NO_THROW(transport->handleCmdAck(ack, 50));
    auto tell = std::make_shared<KcpTellPacket>(1);
    tell->setWnd(16);
    EXPECT_NO_THROW(transport->handleAnyPacket(tell));
    bool dead_link = false;
    transport->setOnErr([&](const SockException &) { dead_link = true; });
    EXPECT_NO_THROW(transport->exercisePrivateStates());
    EXPECT_TRUE(dead_link);
    EXPECT_THROW(transport->setMtu(20), std::runtime_error);
    EXPECT_NO_THROW(transport->setInterval(-100));
    EXPECT_NO_THROW(transport->setInterval(10000));
    EXPECT_NO_THROW(transport->setDelayMode(static_cast<KcpTransport::DelayMode>(-1)));
}

TEST(KcpTransportTest, HandlesOutOfOrderProbeUnknownAndWrongConversationPackets) {
    auto server = std::make_shared<KcpTransport>(true, EventPollerPool::Instance().getPoller());
    std::vector<KcpHeader::Cmd> replies;
    server->setOnWrite([&](const Buffer::Ptr &buffer) {
        size_t offset = 0;
        while (offset + KcpHeader::HEADER_SIZE <= buffer->size()) {
            auto packet = KcpPacket::parse(buffer->data() + offset, buffer->size() - offset);
            if (!packet) break;
            replies.push_back(packet->getCmd());
            offset += packet->getPacketSize();
        }
    });
    semaphore received;
    std::string data;
    server->setOnRead([&](const Buffer::Ptr &buffer) { data += buffer->toString(); received.post(); });

    auto feed = [&](KcpPacket &packet) {
        ASSERT_TRUE(packet.storeToData());
        server->input(std::make_shared<BufferString>(std::string(packet.data(), packet.size())));
    };
    KcpPacket second(42, KcpHeader::Cmd::CMD_PUSH, 1);
    second.setSn(1); second.setFrg(0); second.setWnd(32); second.setUna(0);
    second.getPayloadData()[0] = 'b';
    feed(second);
    KcpPacket first(42, KcpHeader::Cmd::CMD_PUSH, 1);
    first.setSn(0); first.setFrg(0); first.setWnd(32); first.setUna(0);
    first.getPayloadData()[0] = 'a';
    feed(first);
    ASSERT_TRUE(received.wait(1000));
    ASSERT_TRUE(received.wait(1000));
    EXPECT_EQ("ab", data);

    KcpProbePacket probe(42);
    probe.setWnd(0); probe.setSn(2); probe.setUna(0);
    feed(probe);
    KcpTellPacket tell(42);
    tell.setWnd(8); tell.setSn(2); tell.setUna(0);
    feed(tell);
    KcpPacket unknown(42, static_cast<KcpHeader::Cmd>(99), 0);
    unknown.setWnd(8); unknown.setSn(2); unknown.setUna(0);
    feed(unknown);
    KcpPacket wrong(43, KcpHeader::Cmd::CMD_PUSH, 1);
    wrong.setWnd(8); wrong.setSn(2); wrong.setUna(0); wrong.getPayloadData()[0] = 'x';
    feed(wrong);
    server->setOnWrite(nullptr);
    server->setOnRead(nullptr);
}

TEST(SockUtilTest, ConvertsAndComparesIpv4AndIpv6Addresses) {
    EXPECT_TRUE(SockUtil::is_ipv4("127.0.0.1"));
    EXPECT_FALSE(SockUtil::is_ipv4("300.1.1.1"));
    EXPECT_TRUE(SockUtil::is_ipv6("::1"));
    EXPECT_FALSE(SockUtil::is_ipv6("not-an-ip"));

    auto ipv4 = SockUtil::make_sockaddr("127.0.0.1", 1234);
    auto ipv4_copy = SockUtil::make_sockaddr("127.0.0.1", 1234);
    auto ipv4_other = SockUtil::make_sockaddr("127.0.0.1", 4321);
    EXPECT_EQ("127.0.0.1", SockUtil::inet_ntoa(reinterpret_cast<sockaddr *>(&ipv4)));
    EXPECT_EQ(1234, SockUtil::inet_port(reinterpret_cast<sockaddr *>(&ipv4)));
    EXPECT_TRUE(SockUtil::is_same_addr(reinterpret_cast<sockaddr *>(&ipv4), reinterpret_cast<sockaddr *>(&ipv4_copy)));
    EXPECT_FALSE(SockUtil::is_same_addr(reinterpret_cast<sockaddr *>(&ipv4), reinterpret_cast<sockaddr *>(&ipv4_other)));
    EXPECT_EQ(sizeof(sockaddr_in), SockUtil::get_sock_len(reinterpret_cast<sockaddr *>(&ipv4)));

    auto ipv6 = SockUtil::make_sockaddr("::1", 5678);
    EXPECT_EQ("::1", SockUtil::inet_ntoa(reinterpret_cast<sockaddr *>(&ipv6)));
    EXPECT_EQ(5678, SockUtil::inet_port(reinterpret_cast<sockaddr *>(&ipv6)));
    EXPECT_EQ(sizeof(sockaddr_in6), SockUtil::get_sock_len(reinterpret_cast<sockaddr *>(&ipv6)));
}

TEST(SockUtilTest, CreatesTcpAndUdpLoopbackSockets) {
    int listener = SockUtil::listen(0, "127.0.0.1", 4);
    if (listener < 0 && (errno == EPERM || errno == EACCES)) {
        GTEST_SKIP() << "socket creation is prohibited by the execution sandbox";
    }
    ASSERT_GE(listener, 0);
    const auto port = SockUtil::get_local_port(listener);
    EXPECT_GT(port, 0);
    EXPECT_EQ("127.0.0.1", SockUtil::get_local_ip(listener));
    EXPECT_EQ(0, SockUtil::setNoBlocked(listener, true));
    EXPECT_EQ(0, SockUtil::setCloExec(listener, true));
    EXPECT_EQ(0, SockUtil::setRecvBuf(listener, 65536));
    EXPECT_EQ(0, SockUtil::setSendBuf(listener, 65536));

    int client = SockUtil::connect("127.0.0.1", port, false, "127.0.0.1", 0);
    ASSERT_GE(client, 0);
    sockaddr_storage peer = {};
    socklen_t len = sizeof(peer);
    int accepted = ::accept(listener, reinterpret_cast<sockaddr *>(&peer), &len);
    ASSERT_GE(accepted, 0);
    EXPECT_EQ(0, SockUtil::setNoDelay(client, true));
    EXPECT_EQ(0, SockUtil::setKeepAlive(client, true, 1, 1, 1));
    EXPECT_EQ(port, SockUtil::get_peer_port(client));
    EXPECT_EQ("127.0.0.1", SockUtil::get_peer_ip(client));
    EXPECT_EQ(0, SockUtil::getSockError(client));
    ::close(accepted);
    ::close(client);
    ::close(listener);

    int udp = SockUtil::bindUdpSock(0, "127.0.0.1", true);
    ASSERT_GE(udp, 0);
    EXPECT_GT(SockUtil::get_local_port(udp), 0);
    EXPECT_EQ(0, SockUtil::setBroadcast(udp, true));
    EXPECT_EQ(0, SockUtil::dissolveUdpSock(udp));
    ::close(udp);
}

TEST(SockUtilTest, ResolvesLocalhostAndComputesIpv4Ranges) {
    sockaddr_storage address = {};
    EXPECT_TRUE(SockUtil::getDomainIP("localhost", 80, address, AF_INET));
    EXPECT_EQ(80, SockUtil::inet_port(reinterpret_cast<sockaddr *>(&address)));
    const auto range = SockUtil::get_ipv4_range("192.168.1.1", "192.168.1.3");
    EXPECT_EQ(std::vector<std::string>({"192.168.1.1", "192.168.1.2", "192.168.1.3"}), range);
    const auto interfaces = SockUtil::getInterfaceList();
    if (!interfaces.empty()) {
        EXPECT_FALSE(SockUtil::get_local_ip().empty());
    }
}

TEST(SockUtilTest, ExercisesSocketOptionsAndErrorPaths) {
    int tcp = ::socket(AF_INET, SOCK_STREAM, 0);
    if (tcp < 0 && (errno == EPERM || errno == EACCES)) GTEST_SKIP() << "socket creation is prohibited by the execution sandbox";
    ASSERT_GE(tcp, 0);
    EXPECT_EQ(0, SockUtil::setReuseable(tcp, true, false));
    EXPECT_EQ(0, SockUtil::setReuseable(tcp, false, false));
    EXPECT_EQ(0, SockUtil::setNoBlocked(tcp, true));
    EXPECT_EQ(0, SockUtil::setNoBlocked(tcp, false));
    EXPECT_EQ(0, SockUtil::setCloExec(tcp, true));
    EXPECT_EQ(0, SockUtil::setCloExec(tcp, false));
    EXPECT_EQ(0, SockUtil::setRecvBuf(tcp, 32768));
    EXPECT_EQ(0, SockUtil::setSendBuf(tcp, 32768));
    EXPECT_EQ(0, SockUtil::setCloseWait(tcp, 1));
    EXPECT_EQ(0, SockUtil::setCloseWait(tcp, 0));
    EXPECT_LE(SockUtil::setNoSigpipe(tcp), 0);
    EXPECT_EQ(0, SockUtil::getSockError(tcp));
    sockaddr_storage local = {};
    EXPECT_TRUE(SockUtil::get_sock_local_addr(tcp, local));
    sockaddr_storage peer = {};
    EXPECT_FALSE(SockUtil::get_sock_peer_addr(tcp, peer));
    ::close(tcp);

    EXPECT_EQ(-1, SockUtil::setNoDelay(-1, true));
    EXPECT_EQ(-1, SockUtil::setNoBlocked(-1, true));
    EXPECT_EQ(-1, SockUtil::setRecvBuf(-1, 1));
    EXPECT_EQ(-1, SockUtil::setSendBuf(-1, 1));
    EXPECT_EQ(-1, SockUtil::setBroadcast(-1, true));
    EXPECT_EQ(-1, SockUtil::setKeepAlive(-1, true));
    EXPECT_EQ(-1, SockUtil::setCloExec(-1, true));
    EXPECT_EQ(-1, SockUtil::setCloseWait(-1, 0));
    EXPECT_FALSE(SockUtil::get_sock_local_addr(-1, local));
    EXPECT_FALSE(SockUtil::get_sock_peer_addr(-1, peer));
    EXPECT_EQ(0, SockUtil::get_local_port(-1));
    EXPECT_EQ(0, SockUtil::get_peer_port(-1));
    EXPECT_EQ("", SockUtil::get_local_ip(-1));
    EXPECT_EQ("", SockUtil::get_peer_ip(-1));
}

TEST(SockUtilTest, ExercisesUdpMulticastAndAddressUtilities) {
    int udp = SockUtil::bindUdpSock(0, "0.0.0.0", true);
    if (udp < 0 && (errno == EPERM || errno == EACCES)) GTEST_SKIP() << "socket creation is prohibited by the execution sandbox";
    ASSERT_GE(udp, 0);
    EXPECT_EQ(0, SockUtil::setMultiTTL(udp, 8));
    EXPECT_EQ(0, SockUtil::setMultiIF(udp, "127.0.0.1"));
    EXPECT_EQ(0, SockUtil::setMultiLOOP(udp, true));
    EXPECT_EQ(0, SockUtil::joinMultiAddr(udp, "239.255.0.1", "0.0.0.0"));
    EXPECT_EQ(0, SockUtil::leaveMultiAddr(udp, "239.255.0.1", "0.0.0.0"));
    EXPECT_EQ(-1, SockUtil::joinMultiAddr(udp, "invalid", "0.0.0.0"));
    EXPECT_EQ(-1, SockUtil::leaveMultiAddr(udp, "invalid", "0.0.0.0"));
    EXPECT_EQ(-1, SockUtil::joinMultiAddrFilter(udp, "invalid", "127.0.0.1", "0.0.0.0"));
    EXPECT_EQ(-1, SockUtil::leaveMultiAddrFilter(udp, "invalid", "127.0.0.1", "0.0.0.0"));
    ::close(udp);

    sockaddr_storage address = {};
    EXPECT_FALSE(SockUtil::getDomainIP("does-not-exist.invalid", 1, address, AF_INET, SOCK_STREAM, IPPROTO_TCP, 0));
    sockaddr_storage unsupported = {};
    unsupported.ss_family = AF_UNSPEC;
    EXPECT_EQ("", SockUtil::inet_ntoa(reinterpret_cast<sockaddr *>(&unsupported)));
    EXPECT_EQ(0, SockUtil::inet_port(reinterpret_cast<sockaddr *>(&unsupported)));
    EXPECT_TRUE(SockUtil::get_ipv4_range("192.168.1.3", "192.168.1.1").empty());
    EXPECT_TRUE(SockUtil::get_ipv4_range("invalid", "192.168.1.1").empty());
    EXPECT_FALSE(SockUtil::get_ifr_ip("lo").empty());
    EXPECT_FALSE(SockUtil::get_ifr_mask("lo").empty());
    EXPECT_FALSE(SockUtil::get_ifr_name("127.0.0.1").empty());
}
