#ifndef TASKQUEUE_H_
#define TASKQUEUE_H_

#include <mutex>
#include "Util/List.h"
#include "semaphore.h"

namespace toolkit {

//Implemented a task queue based on function objects, which is thread-safe, and the number of tasks in the task queue is controlled by a semaphore
template<typename T>
class TaskQueue {
public:
    //Put a task into the queue
    template<typename C>
    void push_task(C &&task_func) {
        {
            std::lock_guard<decltype(_mutex)> lock(_mutex);
            _queue.emplace_back(std::forward<C>(task_func));
        }
        _sem.post();
    }

    template<typename C>
    void push_task_first(C &&task_func) {
        {
            std::lock_guard<decltype(_mutex)> lock(_mutex);
            _queue.emplace_front(std::forward<C>(task_func));
        }
        _sem.post();
    }

    //Clear the task queue
    void push_exit(size_t n) {
        _sem.post(n);
    }

    //Get a task from the queue and execute it by the executing thread
    bool get_task(T &tsk) {
        _sem.wait();
        std::lock_guard<decltype(_mutex)> lock(_mutex);
        if (_queue.empty()) {
            return false;
        }
        tsk = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }

    size_t size() const {
        std::lock_guard<decltype(_mutex)> lock(_mutex);
        return _queue.size();
    }

private:
    List <T> _queue;
    mutable std::mutex _mutex;
    semaphore _sem;
};

} /* namespace toolkit */
#endif /* TASKQUEUE_H_ */
