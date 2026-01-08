#include <csignal>
#include <iostream>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "Util/logger.h"
#include "Util/TimeTicker.h"
#include "Util/Byte.hpp"
#include "Network/UdpServer.h"
#include "Network/Session.h"

using namespace std;
using namespace toolkit;

static const size_t msg_len = 128; //date len 4 * 128 = 512
static const uint32_t tick_limit = 1* (1024 * 2); //MB
static uint64_t clock_time;

class EchoSession: public Session {
public:
    EchoSession(const Socket::Ptr &sock) :
            Session(sock) {
        DebugL;
    }
    ~EchoSession() {
        DebugL;
    }
    virtual void onRecv(const Buffer::Ptr &buf) override {
        // Handle data sent from the client
        // TraceL << hexdump(buf->data(), buf->size()) <<  " from port:" << get_local_port();
        send(buf);
    }
    virtual void onError(const SockException &err) override{
        // Client disconnects or other reasons cause the object to be removed from TCPServer management
        WarnL << err;
    }
    virtual void onManager() override{
        // Periodically manage the object, such as session timeout check
        // DebugL;
    }

private:
    uint32_t _nTick = 0;
};

//Adjustment of specified session congestion parameters through template full specialization
namespace toolkit {
template <>
class SessionWithKCP<EchoSession> : public EchoSession {
public:
    template <typename... ArgsType>
    SessionWithKCP(ArgsType &&...args)
        : EchoSession(std::forward<ArgsType>(args)...) {
        _kcp_box = std::make_shared<KcpTransport>(true);
        _kcp_box->setOnWrite([&](const Buffer::Ptr &buf) { public_send(buf); });
        _kcp_box->setOnRead([&](const Buffer::Ptr &buf) { public_onRecv(buf); });
        _kcp_box->setOnErr([&](const SockException &ex) { public_onErr(ex); });
        _kcp_box->setInterval(10);
        _kcp_box->setDelayMode(KcpTransport::DelayMode::DELAY_MODE_NO_DELAY);
        _kcp_box->setFastResend(2);
        _kcp_box->setWndSize(1024, 1024);
        _kcp_box->setNoCwnd(true);
        // _kcp_box->setRxMinrto(10);
    }

    ~SessionWithKCP() override { }

    void onRecv(const Buffer::Ptr &buf) override { _kcp_box->input(buf); }

    inline void public_onRecv(const Buffer::Ptr &buf) { EchoSession::onRecv(buf); }
    inline void public_send(const Buffer::Ptr &buf) { EchoSession::send(buf); }
    inline void public_onErr(const SockException &ex) { EchoSession::onError(ex); }

protected:
    ssize_t send(Buffer::Ptr buf) override {
        return _kcp_box->send(std::move(buf));
    }

private:
    KcpTransport::Ptr _kcp_box;
};
}

int main() {
    // Initialize the log module
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    UdpServer::Ptr server(new UdpServer());
    server->start<SessionWithKCP<EchoSession> >(9000);//Listen on port 9000

    // Exit program event handling
    static semaphore sem;
    signal(SIGINT, [](int) { sem.post(); });// Set exit signal
    sem.wait();
    return 0;
}
