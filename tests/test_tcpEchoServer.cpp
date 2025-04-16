#include <csignal>
#include <iostream>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "Util/logger.h"
#include "Util/TimeTicker.h"
#include "Network/TcpServer.h"
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
        TraceL << buf->data() <<  " from port:" << get_local_port();
        send(buf);
    }
    virtual void onError(const SockException &err) override{
        // Client disconnects or other reasons cause the object to be removed from TCPServer management
        WarnL << err;
    }
    virtual void onManager() override{
        // Periodically manage the object, such as session timeout check
        DebugL;
    }

private:
    Ticker _ticker;
};


int main() {
    // Initialize the log module
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    // Load the certificate, the certificate contains the public key and private key
    SSL_Initor::Instance().loadCertificate((exeDir() + "ssl.p12").data());
    SSL_Initor::Instance().trustCertificate((exeDir() + "ssl.p12").data());
    SSL_Initor::Instance().ignoreInvalidCertificate(false);

    TcpServer::Ptr server(new TcpServer());
    server->start<EchoSession>(9000);//Listen to port 9000

    TcpServer::Ptr serverSSL(new TcpServer());
    serverSSL->start<SessionWithSSL<EchoSession> >(9001);//Listen to port 9001

    // Exit program event handling
    static semaphore sem;
    signal(SIGINT, [](int) { sem.post(); });// Set the exit signal
    sem.wait();
    return 0;
}