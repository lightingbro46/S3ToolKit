#include <csignal>
#include <iostream>
#include "Util/util.h"
#include "Util/logger.h"
#include "Util/TimeTicker.h"
#include "Poller/EventPoller.h"

using namespace std;
using namespace toolkit;

/**
 * CPU load balancing test
 * @return
 */
int main() {
    static bool  exit_flag = false;
    signal(SIGINT, [](int) { exit_flag = true; });
    // Set log
    Logger::Instance().add(std::make_shared<ConsoleChannel>());

    Ticker ticker;
    while(!exit_flag){

        if(ticker.elapsedTime() > 1000){
            auto vec = EventPollerPool::Instance().getExecutorLoad();
            _StrPrinter printer;
            for(auto load : vec){
                printer << load << "-";
            }
            DebugL << "CPU load:" << printer;

            EventPollerPool::Instance().getExecutorDelay([](const vector<int> &vec){
                _StrPrinter printer;
                for(auto delay : vec){
                    printer << delay << "-";
                }
                DebugL << "CPU task execution delay:" << printer;
            });
            ticker.resetTime();
        }

        EventPollerPool::Instance().getExecutor()->async([](){
            auto usec = rand() % 4000;
            //DebugL << usec;
            usleep(usec);
        });

        usleep(2000);
    }

    return 0;
}
