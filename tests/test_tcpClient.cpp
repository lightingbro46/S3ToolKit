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
#include "Util/logger.h"
#include "Network/TcpClient.h"
using namespace std;
using namespace toolkit;

class TestClient: public TcpClient {
public:
    using Ptr = std::shared_ptr<TestClient>;
    TestClient():TcpClient() {
        DebugL;
    }
    ~TestClient(){
        DebugL;
    }
protected:
    virtual void onConnect(const SockException &ex) override{
        // Connection established event
        InfoL << (ex ?  ex.what() : "success");
    }
    virtual void onRecv(const Buffer::Ptr &pBuf) override{
        // Data received event
        DebugL << pBuf->data() << " from port:" << get_peer_port();
    }
    virtual void onFlush() override{
        // Send blocked, cache cleared event
        DebugL;
    }
    virtual void onError(const SockException &ex) override{
        // Disconnected event, usually EOF
        WarnL << ex.what();
    }
    virtual void onManager() override{
        // Periodically send data to the server
        auto buf = BufferRaw::create();
        if(buf){
            buf->assign("[BufferRaw]\0");
            (*this) << _nTick++ << " "
                    << 3.14 << " "
                    << string("string") << " "
                    <<(Buffer::Ptr &)buf;
        }
    }
private:
    int _nTick = 0;
};


int main() {
    // Set up the logging system
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    TestClient::Ptr client(new TestClient());//Must use smart pointers
    client->startConnect("127.0.0.1",9000);//Connect to the server

    TcpClientWithSSL<TestClient>::Ptr clientSSL(new TcpClientWithSSL<TestClient>());//Must use smart pointers
    clientSSL->startConnect("127.0.0.1",9001);//Connect to the server

    // Exit program event handling
    static semaphore sem;
    signal(SIGINT, [](int) { sem.post(); });// Set the exit signal
    sem.wait();
    return 0;
}