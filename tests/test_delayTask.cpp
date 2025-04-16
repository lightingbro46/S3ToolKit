#include <csignal>
#include <iostream>
#include "Util/util.h"
#include "Util/logger.h"
#include "Util/TimeTicker.h"
#include "Util/onceToken.h"
#include "Poller/EventPoller.h"

using namespace std;
using namespace toolkit;

int main() {
    // Set log
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    Ticker ticker0;
    int nextDelay0 = 50;
    std::shared_ptr<onceToken> token0 = std::make_shared<onceToken>(nullptr,[](){
        TraceL << "task 0 is cancelled, and can immediately trigger the release of variables captured by the lambad expression!";
    });
    auto tag0 = EventPollerPool::Instance().getPoller()->doDelayTask(nextDelay0, [&,token0]() {
        TraceL << "task 0(Fixed delay repetition tasks),Expected sleep time :" << nextDelay0 << " Actual sleep time" << ticker0.elapsedTime();
        ticker0.resetTime();
        return nextDelay0;
    });
    token0 = nullptr;

    Ticker ticker1;
    int nextDelay1 = 50;
    auto tag1 = EventPollerPool::Instance().getPoller()->doDelayTask(nextDelay1, [&]() {
        DebugL << "task 1(Variable delay repetition tasks),Expected sleep time :" << nextDelay1 << " Actual sleep time" << ticker1.elapsedTime();
        ticker1.resetTime();
        nextDelay1 += 1;
        return nextDelay1;
    });

    Ticker ticker2;
    int nextDelay2 = 3000;
    auto tag2 = EventPollerPool::Instance().getPoller()->doDelayTask(nextDelay2, [&]() {
        InfoL << "task 2(Single delay task),Expected sleep time :" << nextDelay2 << " Actual sleep time" << ticker2.elapsedTime();
        return 0;
    });

    Ticker ticker3;
    int nextDelay3 = 50;
    auto tag3 = EventPollerPool::Instance().getPoller()->doDelayTask(nextDelay3, [&]() -> uint64_t {
        throw std::runtime_error("task 2(Exceed exceptions in test delay task,Will cause the delay task to no longer be continued)");
    });


    sleep(2);
    tag0->cancel();
    tag1->cancel();
    WarnL << "Cancel task 0, 1";

    // Exit program event handling
    static semaphore sem;
    signal(SIGINT, [](int) { sem.post(); });// Set the exit signal
    sem.wait();
    return 0;
}
