/*
 * Copyright (c) 2025 The S3ToolKit project authors. All Rights Reserved.
 *
 * This file is part of S3ToolKit(https://github.com/S3MediaKit/S3ToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef Timer_h
#define Timer_h

#include <functional>
#include "EventPoller.h"

namespace toolkit {

class Timer {
public:
    using Ptr = std::shared_ptr<Timer>;

    /**
     * Constructs a timer
     * @param second Timer repeat interval in seconds
     * @param cb Timer task, returns true to repeat the next task, otherwise does not repeat. If an exception is thrown in the task, it defaults to repeating the next task
     * @param poller EventPoller object, can be nullptr
     */
    Timer(float second, const std::function<bool()> &cb, const EventPoller::Ptr &poller);
    ~Timer();

private:
    std::weak_ptr<EventPoller::DelayTask> _tag;
    //Timer keeps a strong reference to EventPoller
    EventPoller::Ptr _poller;
};

}  // namespace toolkit
#endif /* Timer_h */
