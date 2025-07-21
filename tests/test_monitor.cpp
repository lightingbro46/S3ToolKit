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

    // chuyển đổi domain sang ip
    struct sockaddr_storage out;
    if (SockUtil::getDomainIP("vmscmstest.vtscloud.vn", 443, out)) {
        DebugL << "Domain->Ip: " << getIP((sockaddr *) &out);
    }

    // lấy danh sách tất cả interface network và ip tương ứng
    auto if_list = SockUtil::getInterfaceList();
    for (const auto &if_item : if_list) {
        DebugL << "=========Net if=========";
        for (const auto &it : if_item) {
            DebugL << it.first << ": " << it.second;
        }
    }

    //lấy ip local
    string local_ip = SockUtil::get_local_ip();
    DebugL << "local ip: " << local_ip;

    // kiểm tra hệ thống hỗ trợ ipv6 ko
    bool isSupportIpv6 = SockUtil::support_ipv6();
    DebugL << "Support ipv6: " << (isSupportIpv6 ? "true" : "false");

    if (isSupportIpv6) {
        // get ipv6
    }

    int i = 0;
    while(!exitProgram){
       
        sleep(1);
    }
    return 0;
}





