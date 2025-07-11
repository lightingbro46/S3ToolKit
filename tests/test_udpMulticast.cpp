#include <csignal>
#include <iostream>
#include "Util/util.h"
#include "Util/logger.h"
#include "Network/Socket.h"

using namespace std;
using namespace toolkit;

// Main thread exit flag
bool exitProgram = false;

// Assign struct sockaddr
void makeAddr(struct sockaddr_storage *out,const char *ip,uint16_t port){
    *out = SockUtil::make_sockaddr(ip, port);
}

// Get IP string from struct sockaddr
string getIP(struct sockaddr *addr){
    return SockUtil::inet_ntoa(addr);
}

string multicast_addr = "239.255.255.250";
int port = 1900;

int main() {
    // Set program exit signal handling function
    signal(SIGINT, [](int){exitProgram = true;});
    // Set up logging system
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    Socket::Ptr sockRecv = Socket::createSocket();//Create a UDP data receiving port
    Socket::Ptr sockSend = Socket::createSocket();//Create a UDP data sending port
    sockRecv->bindUdpSock(port);//Receive UDP binding port 9001
    sockSend->bindUdpSock(0, "0.0.0.0");//Send UDP random port

    // Join multicast
    if (-1 == SockUtil::joinMultiAddr(sockRecv->rawFD(), multicast_addr.data())) {
        ErrorL << "join multicast fail!";
    }

    // set read callback
    sockRecv->setOnRead([](const Buffer::Ptr &buf, struct sockaddr *addr , int){
        // Data received callback
        DebugL << "recv data form " << getIP(addr) << ":" << buf->data();
    });

    struct sockaddr_storage addrDst;
    makeAddr(&addrDst, multicast_addr.data() , port);//UDP data sending address
    //	sockSend->bindPeerAddr(&addrDst);

    int i = 0;
    while(!exitProgram){
        // Send data to the other side every second
        sockSend->send(to_string(i++), (struct sockaddr *)&addrDst);
        InfoL << "send";
        sleep(1);
    }
    return 0;
}





