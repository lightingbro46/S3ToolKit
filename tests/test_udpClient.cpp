#include <csignal>
#include <iostream>
#include "Util/logger.h"
#include "Network/UdpClient.h"
using namespace std;
using namespace toolkit;

class TestClient: public UdpClient {
public:
    using Ptr = std::shared_ptr<TestClient>;
    TestClient():UdpClient() {
        DebugL;
    }
    ~TestClient(){
        DebugL;
    }
protected:
    virtual void onRecvFrom(const Buffer::Ptr &buf, struct sockaddr *addr, int addr_len) override{
        if (!addr) {
            DebugL << " recvfrom ip: " << SockUtil::inet_ntoa(addr) << ", port: " << SockUtil::inet_port(addr);
        }
        TraceL << hexdump(buf->data(), buf->size());
    }

    virtual void onError(const SockException &ex) override{
        // Disconnected event, usually EOF
        WarnL << ex.what();
    }

    virtual void onManager() override{
        // Periodically send data to the server
        std::string msg  =(StrPrinter << _nTick++ << " "
                           << 3.14 << " "
                           << "string" << " "
                           << "[BufferRaw]\0");
        (*this) << msg;
    }

private:
    int _nTick = 0;
};


int main() {
    // Set up the logging system
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    TestClient::Ptr client(new TestClient());//Must use smart pointers
    client->startConnect("127.0.0.1", 9000);//Connect to server

    // Exit program event handling
    static semaphore sem;
    signal(SIGINT, [](int) { sem.post(); });// Set exit signal
    sem.wait();
    return 0;
}
