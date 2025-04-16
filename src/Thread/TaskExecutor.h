#ifndef S3TOOLKIT_TASKEXECUTOR_H
#define S3TOOLKIT_TASKEXECUTOR_H

#include <mutex>
#include <memory>
#include <functional>
#include "Util/List.h"
#include "Util/util.h"

namespace toolkit {

/**
 * CPU Load Calculator
*/
class ThreadLoadCounter {
public:
    /**
     * Constructor
     * @param max_size Number of statistical samples
     * @param max_usec Statistical time window, i.e., the CPU load rate for the most recent {max_usec}
     */
    ThreadLoadCounter(uint64_t max_size, uint64_t max_usec);
    ~ThreadLoadCounter() = default;

    /**
     * Thread enters sleep
     */
    void startSleep();

    /**
     * Wake up from sleep, end sleep
     */
    void sleepWakeUp();

    /**
     * Returns the current thread's CPU usage rate, ranging from 0 to 100
     * @return Current thread's CPU usage rate
     */
    int load();

private:
    struct TimeRecord {
        TimeRecord(uint64_t tm, bool slp) {
            _time = tm;
            _sleep = slp;
        }

        bool _sleep;
        uint64_t _time;
    };

private:
    bool _sleeping = true;
    uint64_t _last_sleep_time;
    uint64_t _last_wake_time;
    uint64_t _max_size;
    uint64_t _max_usec;
    std::mutex _mtx;
    List<TimeRecord> _time_list;
};

class TaskCancelable : public noncopyable {
public:
    TaskCancelable() = default;
    virtual ~TaskCancelable() = default;
    virtual void cancel() = 0;
};

template<class R, class... ArgTypes>
class TaskCancelableImp;

template<class R, class... ArgTypes>
class TaskCancelableImp<R(ArgTypes...)> : public TaskCancelable {
public:
    using Ptr = std::shared_ptr<TaskCancelableImp>;
    using func_type = std::function<R(ArgTypes...)>;

    ~TaskCancelableImp() = default;

    template<typename FUNC>
    TaskCancelableImp(FUNC &&task) {
        _strongTask = std::make_shared<func_type>(std::forward<FUNC>(task));
        _weakTask = _strongTask;
    }

    void cancel() override {
        _strongTask = nullptr;
    }

    operator bool() {
        return _strongTask && *_strongTask;
    }

    void operator=(std::nullptr_t) {
        _strongTask = nullptr;
    }

    R operator()(ArgTypes ...args) const {
        auto strongTask = _weakTask.lock();
        if (strongTask && *strongTask) {
            return (*strongTask)(std::forward<ArgTypes>(args)...);
        }
        return defaultValue<R>();
    }

    template<typename T>
    static typename std::enable_if<std::is_void<T>::value, void>::type
    defaultValue() {}

    template<typename T>
    static typename std::enable_if<std::is_pointer<T>::value, T>::type
    defaultValue() {
        return nullptr;
    }

    template<typename T>
    static typename std::enable_if<std::is_integral<T>::value, T>::type
    defaultValue() {
        return 0;
    }

protected:
    std::weak_ptr<func_type> _weakTask;
    std::shared_ptr<func_type> _strongTask;
};

using TaskIn = std::function<void()>;
using Task = TaskCancelableImp<void()>;

class TaskExecutorInterface {
public:
    TaskExecutorInterface() = default;
    virtual ~TaskExecutorInterface() = default;

    /**
     * Asynchronously execute a task
     * @param task Task
     * @param may_sync Whether to allow synchronous execution of the task
     * @return Whether the task was added successfully
     */
    virtual Task::Ptr async(TaskIn task, bool may_sync = true) = 0;

    /**
     * Asynchronously execute a task with the highest priority
     * @param task Task
     * @param may_sync Whether to allow synchronous execution of the task
     * @return Whether the task was added successfully
     */
    virtual Task::Ptr async_first(TaskIn task, bool may_sync = true);

    /**
     * Synchronously execute a task
     * @param task
     * @return
     */
    void sync(const TaskIn &task);

    /**
     * Synchronously execute a task with the highest priority
     * @param task
     * @return
     */
    void sync_first(const TaskIn &task);
};

/**
 * Task Executor
*/
class TaskExecutor : public ThreadLoadCounter, public TaskExecutorInterface {
public:
    using Ptr = std::shared_ptr<TaskExecutor>;

    /**
     * Constructor
     * @param max_size cpu load statistics sample count
     * @param max_usec CPU load statistics time window size
     */
    TaskExecutor(uint64_t max_size = 32, uint64_t max_usec = 2 * 1000 * 1000);
    ~TaskExecutor() = default;
};

class TaskExecutorGetter {
public:
    using Ptr = std::shared_ptr<TaskExecutorGetter>;

    virtual ~TaskExecutorGetter() = default;

    /**
     * Get the task executor
     * @return Task executor
     */
    virtual TaskExecutor::Ptr getExecutor() = 0;

    /**
     * Get the number of actuators
     */
    virtual size_t getExecutorSize() const = 0;
};

class TaskExecutorGetterImp : public TaskExecutorGetter {
public:
    TaskExecutorGetterImp() = default;
    ~TaskExecutorGetterImp() = default;

    /**
     * Obtain the idle task executor according to the thread load situation
     * @return Task executor
     */
    TaskExecutor::Ptr getExecutor() override;

    /**
     * Get the load rate of all threads
     * @return Load rate for all threads
     */
    std::vector<int> getExecutorLoad();

    /**
     * Get all thread task execution delays in milliseconds
     * This function can also roughly know the thread load situation
     * @return
     */
    void getExecutorDelay(const std::function<void(const std::vector<int> &)> &callback);

    /**
     * Iterate through all threads
     */
    void for_each(const std::function<void(const TaskExecutor::Ptr &)> &cb);

    /**
     * Get the number of threads
     */
    size_t getExecutorSize() const override;

protected:
    size_t addPoller(const std::string &name, size_t size, int priority, bool register_thread, bool enable_cpu_affinity = true);

protected:
    size_t _thread_pos = 0;
    std::vector<TaskExecutor::Ptr> _threads;
};

}//toolkit
#endif //S3TOOLKIT_TASKEXECUTOR_H
