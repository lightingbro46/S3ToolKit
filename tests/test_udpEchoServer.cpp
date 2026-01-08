#include <csignal>
#include <iostream>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "Util/util.h"
#include "Util/logger.h"
#include "Util/TimeTicker.h"
#include "Network/UdpServer.h"
#include "Network/Session.h"

using namespace std;
using namespace toolkit;

class EchoSession: public Session {
public:
    EchoSession(const Socket::Ptr &sock) :
            Session(sock) {
        DebugL;
    }
    ~EchoSession() {
        DebugL;
    }
    virtual void onRecv(const Buffer::Ptr &buf) override{
        // Handle data sent from the client
        // TraceL << hexdump(buf->data(), buf->size()) <<  " from port:" << get_local_port();
        send(buf);
    }
    virtual void onError(const SockException &err) override{
        // Client disconnects or other reasons cause the object to be removed from TCPServer management
        WarnL << err;
    }
    virtual void onManager() override {
        // Periodically manage the object, such as session timeout check
        // DebugL;
    }

private:
    Ticker _ticker;
};


int main() {
    // Initialize the log module
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    UdpServer::Ptr server(new UdpServer());
    server->start<EchoSession>(9000);//Listen on port 9000

    // Exit program event handling
    static semaphore sem;
    signal(SIGINT, [](int) { sem.post(); });// Set exit signal
    sem.wait();
    return 0;
}
