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
#include <atomic>
#include <iostream>
#include "Util/logger.h"
#include "Util/TimeTicker.h"
#include "Thread/ThreadPool.h"

using namespace std;
using namespace toolkit;

int main() {
    signal(SIGINT,[](int ){
        exit(0);
    });
    // Initialize the logging system
    Logger::Instance().add(std::make_shared<ConsoleChannel> ());

    atomic_llong count(0);
    ThreadPool pool(1,ThreadPool::PRIORITY_HIGHEST, false);

    Ticker ticker;
    for (int i = 0 ; i < 1000*10000;++i){
        pool.async([&](){
           if(++count >= 1000*10000){
               InfoL << "The total time is spent executing 10 million tasks:" << ticker.elapsedTime() << "ms";
           }
        });
    }
    InfoL << "Time-consuming to join the team of 10 million tasks:" << ticker.elapsedTime() << "ms" << endl;
    uint64_t  lastCount = 0 ,nowCount = 1;
    ticker.resetTime();
    // The thread starts here
    pool.start();
    while (true){
        sleep(1);
        nowCount = count.load();
        InfoL << "Number of tasks executed per second:" << nowCount - lastCount;
        if(nowCount - lastCount == 0){
            break;
        }
        lastCount = nowCount;
    }
    return 0;
}
