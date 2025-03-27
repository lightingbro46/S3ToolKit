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
#include <random>
#include "Util/util.h"
#include "Util/logger.h"
#include "Util/ResourcePool.h"
#include "Thread/threadgroup.h"
#include <list>

using namespace std;
using namespace toolkit;

// Program exit flag
bool g_bExitFlag = false;


class string_imp : public string{
public:
    template<typename ...ArgTypes>
    string_imp(ArgTypes &&...args) : string(std::forward<ArgTypes>(args)...){
        DebugL << "Create a string object:" << this << " " << *this;
    };
    ~string_imp(){
        WarnL << "Destroy string objects:" << this << " " << *this;
    }
};


// Background thread task
void onRun(ResourcePool<string_imp> &pool,int threadNum){
    std::random_device rd;
    while(!g_bExitFlag){
        // Get an available object from the loop pool
        auto obj_ptr = pool.obtain();
        if(obj_ptr->empty()){
            // This object is brand new and unused
            InfoL << "Background thread " << threadNum << ":" << "obtain a emptry object!";
        }else{
            // This object is looped for reuse
            InfoL << "Background thread " << threadNum << ":" << *obj_ptr;
        }
        // Mark this object as used by the current thread
        obj_ptr->assign(StrPrinter << "keeped by thread:" << threadNum );

        // Random sleep to disrupt the loop usage order
        usleep( 1000 * (rd()% 10));
        obj_ptr.reset();//Release manually, you can also comment this code. According to the principle of RAII, the object will be automatically released and re-entered into the loop queue
        usleep( 1000 * (rd()% 1000));
    }
}

int main() {
    // Initialize log
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    // Loop pool of size 50
    ResourcePool<string_imp> pool;
    pool.setSize(50);

    // Get an object that will be held by the main thread and will not be obtained and assigned by the background thread
    auto reservedObj = pool.obtain();
    // Assign the object in the main thread
    reservedObj->assign("This is a reserved object , and will never be used!");

    thread_group group;
    // Create 4 background threads, these 4 threads simulate the usage scenario of a loop pool,
    // In theory, the 4 threads can occupy at most 4 objects at the same time


    WarnL << "Main thread printing:" << "Start the test, the object that the main thread has obtained should not be obtained by the background thread.:" << *reservedObj;

    for(int i = 0 ;i < 4 ; ++i){
        group.create_thread([i,&pool](){
            onRun(pool,i);
        });
    }

    // Wait for 3 seconds, at this time, the objects available in the loop pool have been used at least once
    sleep(3);

    // However, since reservedObj has been held by the main thread, the background threads cannot obtain the object
    // So its value should not have been overwritten
    WarnL << "Main thread printing: The object is still held by the main thread, and its value should remain unchanged:" << *reservedObj;

    // Get a reference to the object
    auto &objref = *reservedObj;

    // Explicitly release the object, allowing it to re-enter the loop queue, at this time the object should be held and assigned by the background thread
    reservedObj.reset();

    WarnL << "Main thread print: The object has been released, it should be retrieved by the background thread and overwritten the value";

    // Sleep for another 3 seconds, allowing reservedObj to be looped and used by the background thread
    sleep(3);

    // At this time, reservedObj is still in the loop pool, the reference should still be valid, but the value should have been overwritten
    WarnL << "Main thread print: The object has been assigned to:" << objref << endl;

    {
        WarnL << "Main thread print: Start testing and actively abandoning the loop function";

        List<decltype(pool)::ValuePtr> objlist;
        for (int i = 0; i < 8; ++i) {
            reservedObj = pool.obtain();
            string str = StrPrinter << i << " " << (i % 2 == 0 ? "This object will be out of loop pool management" : "This object will return to the loop pool");
            reservedObj->assign(str);
            reservedObj.quit(i % 2 == 0);
            objlist.emplace_back(reservedObj);
        }
    }
    sleep(3);

    // Notify background thread to exit
    g_bExitFlag = true;
    // Wait for background thread to exit
    group.join_all();
    return 0;
}











