/*
 * Copyright (c) 2025 The S3ToolKit project authors. All Rights Reserved.
 *
 * This file is part of S3ToolKit(https://github.com/S3MediaKit/S3ToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <iostream>
#include "Util/CMD.h"
#include "Util/logger.h"
#include "Util/util.h"
#include "Network/TcpClient.h"
#include <csignal>

using namespace std;
using namespace toolkit;

class TestClient: public TcpClient {
public:
    using Ptr = std::shared_ptr<TestClient>;
    TestClient() : TcpClient(){}
    ~TestClient(){}
    void connect(const string &strUrl, uint16_t iPort,float fTimeoutSec){
        startConnect(strUrl,iPort,fTimeoutSec);
    }
    void disconnect(){
        shutdown();
    }
    size_t commit(const string &method,const string &path,const string &host) {
        string strGet = StrPrinter
                << method
                << " "
                << path
                << " HTTP/1.1\r\n"
                << "Host: " << host << "\r\n"
                << "Connection: keep-alive\r\n"
                << "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_12_1) "
                        "AppleWebKit/537.36 (KHTML, like Gecko) "
                        "Chrome/58.0.3029.110 Safari/537.36\r\n"
                << "Accept-Encoding: gzip, deflate, sdch\r\n"
                << "Accept-Language: zh-CN,zh;q=0.8,en;q=0.6\r\n\r\n";
        DebugL << "\r\n" << strGet;
        return SockSender::send(strGet);
    }

protected:
    virtual void onConnect(const SockException &ex) override{
        // Connection established event
        InfoL << (ex ?  ex.what() : "success");
    }
    virtual void onRecv(const Buffer::Ptr &pBuf) override{
        // Data received event
        DebugL << pBuf->data();
    }
    virtual void onFlush() override{
        // Buffer cleared after send blocking event
        DebugL;
    }
    virtual void onError(const SockException &ex) override{
        // Disconnected event, usually EOF
        WarnL << ex;
    }
};

// Command (http)
class CMD_http: public CMD {
public:
    CMD_http(){
        _client.reset(new TestClient);
        _parser.reset(new OptionParser([this](const std::shared_ptr<ostream> &stream,mINI &args){
            // All options parsed, trigger this callback, we can do some global operations here
            if(hasKey("connect")){
                // Initiate connection operation
                connect(stream);
                return;
            }
            if(hasKey("commit")){
                commit(stream);
                return;
            }
        }));

        (*_parser) << Option('T', "type", Option::ArgRequired, nullptr, true, "Application mode, 0: legacy mode, 1: shell mode", nullptr);

        (*_parser) << Option('s',/*This option is abbreviated, if it is \x00, it means there is no abbreviation*/
                             "server",/*The full name of this option, each option must have a full name; it must not be null or empty string*/
                             Option::ArgRequired,/*This option must be followed by a value*/
                             "www.baidu.com:80",/*This option default value*/
                             false,/*Whether this option must be assigned a value, if there is no default value and is ArgRequired, the user must provide this parameter otherwise an exception will be thrown*/
                             "TCP server address, separated port number by colon",/*This option specifies text*/
                             [this](const std::shared_ptr<ostream> &stream, const string &arg){/*A callback parsed to this option*/
                                 if(arg.find(":") == string::npos){
                                     // Interrupt subsequent option parsing and parsing completion callback operations
                                     throw std::runtime_error("\tThe address must specify the port number.");
                                 }
                                 // If return false, ignore subsequent option parsing
                                 return true;
                             });

        (*_parser) << Option('d', "disconnect", Option::ArgNone, nullptr ,false, "Whether to disconnect",
                             [this](const std::shared_ptr<ostream> &stream, const string &arg){
                                 // Disconnect operation, so we don't parse subsequent parameters
                                 disconnect(stream);
                                 return false;
                             });

        (*_parser) << Option('c', "connect", Option::ArgNone, nullptr, false, "Initiate the tcp connect operation", nullptr);
        (*_parser) << Option('t', "time_out", Option::ArgRequired, "3",false, "Connection timeout", nullptr);
        (*_parser) << Option('m', "method", Option::ArgRequired, "GET",false, "HTTP methods, such as GET and POST", nullptr);
        (*_parser) << Option('p', "path", Option::ArgRequired, "/index.html",false, "HTTP url path", nullptr);
        (*_parser) << Option('C', "commit", Option::ArgNone, nullptr, false, "Submit HTTP request", nullptr);



    }

    ~CMD_http() {}

    const char *description() const override {
        return "http test client";
    }

private:
    void connect(const std::shared_ptr<ostream> &stream){
        (*stream) << "Connect operation" << endl;
        _client->connect(splitedVal("server")[0],splitedVal("server")[1],(*this)["time_out"]);
    }
    void disconnect(const std::shared_ptr<ostream> &stream){
        (*stream) << "Disconnect operation" << endl;
        _client->disconnect();
    }
    void commit(const std::shared_ptr<ostream> &stream){
        (*stream) << "commit operation" << endl;
        _client->commit((*this)["method"],(*this)["path"],(*this)["server"]);
    }

private:
    TestClient::Ptr _client;
};



int main(int argc,char *argv[]){
    REGIST_CMD(http);
    signal(SIGINT,[](int ){
        exit(0);
    });
    try{
        CMD_DO("http",argc,argv);
    }catch (std::exception &ex){
        cout << ex.what() << endl;
        return 0;
    }
    if(GET_CMD("http")["type"] == 0){
        cout << "Traditional mode, the program has been exited, please try shell mode" << endl;
        return 0;
    }
    GET_CMD("http").delOption("type");
    // Initialize environment
    Logger::Instance().add(std::shared_ptr<ConsoleChannel>(new ConsoleChannel()));
    Logger::Instance().setWriter(std::shared_ptr<LogWriter>(new AsyncLogWriter()));

    cout << "> Welcome to command mode, you can enter the \"help\" command to get help" << endl;
    string cmd_line;
    while(cin.good()){
        try{
            cout << "> ";
            getline(cin,cmd_line);
            CMDRegister::Instance()(cmd_line);
        }catch (ExitException &){
            break;
        }catch (std::exception &ex){
            cout << ex.what() << endl;
        }
    }
    return 0;
}