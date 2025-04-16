#include <csignal>
#include <iostream>
#include "Util/util.h"
#include "Util/logger.h"
#include "Util/TimeTicker.h"
#include "Poller/Timer.h"

using namespace std;
using namespace toolkit;

int main() {
    // Set log
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());


    Ticker ticker0;
    Timer::Ptr timer0 = std::make_shared<Timer>(0.5f,[&](){
        TraceL << "timer0,Repeat:" << ticker0.elapsedTime();
        ticker0.resetTime();
        return true;
    }, nullptr);

    Timer::Ptr timer1 = std::make_shared<Timer>(1.0f,[](){
        DebugL << "timer1,Will no longer be repeated";
        return false;
    },nullptr);

    Ticker ticker2;
    Timer::Ptr timer2 = std::make_shared<Timer>(2.0f,[&]() -> bool {
        InfoL << "timer2,Exceed exceptions in test task:" << ticker2.elapsedTime();
        ticker2.resetTime();
        throw std::runtime_error("timer2,Exceed exceptions in test task");
    },nullptr);

    // Exit program event handling
    static semaphore sem;
    signal(SIGINT, [](int) { sem.post(); });// Set the exit signal
    sem.wait();
    return 0;
}
