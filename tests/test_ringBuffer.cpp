#include <csignal>
#include <iostream>
#include "Util/logger.h"
#include "Util/util.h"
#include "Util/RingBuffer.h"
#include "Thread/threadgroup.h"
#include <list>

using namespace std;
using namespace toolkit;

// Ring buffer write thread exit flag
bool g_bExitWrite = false;

// A ring buffer of 30 string objects
RingBuffer<string>::Ptr g_ringBuf(new RingBuffer<string>(30));

// Write event callback function
void onReadEvent(const string &str){
    // Read event mode
    DebugL << str;
}

// Ring buffer destruction event
void onDetachEvent(){
    WarnL;
}

// Write ring buffer task
void doWrite(){
    int i = 0;
    while(!g_bExitWrite){
        // Write data to the ring buffer every 100ms
        g_ringBuf->write(to_string(++i),true);
        usleep(100 * 1000);
    }

}
int main() {
    // Initialize log
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    auto poller = EventPollerPool::Instance().getPoller();
    RingBuffer<string>::RingReader::Ptr ringReader;
    poller->sync([&](){
        // Get a reader from the ring buffer
        ringReader = g_ringBuf->attach(poller);

        // Set read event
        ringReader->setReadCB([](const string &pkt){
            onReadEvent(pkt);
        });

        // Set ring buffer destruction event
        ringReader->setDetachCB([](){
            onDetachEvent();
        });
    });


    thread_group group;
    // Write thread
    group.create_thread([](){
        doWrite();
    });

    // Test for 3 seconds
    sleep(3);

    // Notify write thread to exit
    g_bExitWrite = true;
    // Wait for write thread to exit
    group.join_all();

    // Release ring buffer, triggering Detach event asynchronously
    g_ringBuf.reset();
    // Wait for asynchronous Detach event trigger
    sleep(1);
    // Remove reference to EventPoller object
    ringReader.reset();
    sleep(1);
    return 0;
}











