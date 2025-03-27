/*
 * Copyright (c) 2025 The S3ToolKit project authors. All Rights Reserved.
 *
 * This file is part of S3ToolKit(https://github.com/S3MediaKit/S3ToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

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

int main() {
    // Set program exit signal handling function
    signal(SIGINT, [](int){exitProgram = true;});
    // Set up logging system
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    Socket::Ptr sockRecv = Socket::createSocket();//Create a UDP data receiving port
    Socket::Ptr sockSend = Socket::createSocket();//Create a UDP data sending port
    sockRecv->bindUdpSock(9001);//Receive UDP binding port 9001
    sockSend->bindUdpSock(0, "0.0.0.0");//Send UDP random port

    sockRecv->setOnRead([](const Buffer::Ptr &buf, struct sockaddr *addr , int){
        // Data received callback
        DebugL << "recv data form " << getIP(addr) << ":" << buf->data();
    });

    struct sockaddr_storage addrDst;
    makeAddr(&addrDst,"127.0.0.1",9001);//UDP data sending address
//	sockSend->bindPeerAddr(&addrDst);
    int i = 0;
    while(!exitProgram){
        // Send data to the other side every second
        sockSend->send(to_string(i++), (struct sockaddr *)&addrDst);
        sleep(1);
    }
    return 0;
}





