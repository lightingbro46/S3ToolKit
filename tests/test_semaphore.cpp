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
#include "Util/TimeTicker.h"
#include "Util/logger.h"
#include "Thread/threadgroup.h"
#include "Thread/semaphore.h"

using namespace std;
using namespace toolkit;


#define MAX_TASK_SIZE (1000 * 10000)
semaphore g_sem;//Signal volume
atomic_llong g_produced(0);
atomic_llong g_consumed(0);

// Consumer thread
void onConsum() {
    while (true) {
        g_sem.wait();
        if (++g_consumed > g_produced) {
            // If this log is printed, it indicates a bug
            ErrorL << g_consumed << " > " << g_produced;
        }
    }
}

// Producer thread
void onProduce() {
    while(true){
        ++ g_produced;
        g_sem.post();
        if(g_produced >= MAX_TASK_SIZE){
            break;
        }
    }
}
int main() {
    // Initialize log
    Logger::Instance().add(std::make_shared<ConsoleChannel>());

    Ticker ticker;
    thread_group thread_producer;
    for (size_t i = 0; i < thread::hardware_concurrency(); ++i) {
        thread_producer.create_thread([]() {
            // 1 producer thread
            onProduce();
        });
    }

    thread_group thread_consumer;
    for (int i = 0; i < 4; ++i) {
        thread_consumer.create_thread([i]() {
            // 4 consumer threads
            onConsum();
        });
    }



    // Wait for all producer threads to exit
    thread_producer.join_all();
    DebugL << "Producer thread exits, time-consuming:" << ticker.elapsedTime() << "ms," << "Number of production tasks:" << g_produced << ",Number of consumption tasks:" << g_consumed;

    int i = 5;
    while(-- i){
        DebugL << "Countdown to program exit:" << i << ",Number of consumption tasks:" << g_consumed;
        sleep(1);
    }

    // The program may force exit and core dump; when the program exits, the number of produced tasks should be consistent with the number of consumed tasks
    WarnL << "Forced shutdown of the consumer thread, may trigger core dump" ;
    return 0;
}
