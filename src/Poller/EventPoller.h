#ifndef EventPoller_h
#define EventPoller_h

#include <mutex>
#include <thread>
#include <string>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include "PipeWrap.h"
#include "Util/logger.h"
#include "Util/List.h"
#include "Thread/TaskExecutor.h"
#include "Thread/ThreadPool.h"
#include "Network/Buffer.h"
#include "Network/BufferSock.h"

#if defined(__linux__) || defined(__linux)
#define HAS_EPOLL
#endif //__linux__

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define HAS_KQUEUE
#endif // __APPLE__

#if defined(HAS_EPOLL) || defined(HAS_KQUEUE)
#if defined(_WIN32)
using epoll_fd = void *;
constexpr epoll_fd INVALID_EVENT_FD = nullptr;
#else
using epoll_fd = int;
constexpr epoll_fd INVALID_EVENT_FD = -1;
#endif
#endif

namespace toolkit {

class EventPoller : public TaskExecutor, public AnyStorage, public std::enable_shared_from_this<EventPoller> {
public:
    friend class TaskExecutorGetterImp;

    using Ptr = std::shared_ptr<EventPoller>;
    using PollEventCB = std::function<void(int event)>;
    using PollCompleteCB = std::function<void(bool success)>;
    using DelayTask = TaskCancelableImp<uint64_t(void)>;

    typedef enum {
        Event_Read = 1 << 0, // Read event
        Event_Write = 1 << 1, // Write event
        Event_Error = 1 << 2, // Error event
        Event_LT = 1 << 3,   // horizontal trigger
    } Poll_Event;

    ~EventPoller();

    /**
     * Gets the first EventPoller instance from the EventPollerPool singleton,
     * This interface is preserved for compatibility with old code.
     * @return singleton
     */
    static EventPoller &Instance();

    /**
     * Adds an event listener
     * @param fd The file descriptor to listen to
     * @param event The event type, e.g. Event_Read | Event_Write
     * @param cb The event callback function
     * @return -1: failed, 0: success
     */
    int addEvent(int fd, int event, PollEventCB cb);

    /**
     * Deletes an event listener
     * @param fd The file descriptor to stop listening to
     * @param cb The callback function for successful deletion
     * @return -1: failed, 0: success
     */
    int delEvent(int fd, PollCompleteCB cb = nullptr);

    /**
     * Modifies the event type being listened to
     * @param fd The file descriptor to modify
     * @param event The new event type, e.g. Event_Read | Event_Write
     * @return -1: failed, 0: success
     */
    int modifyEvent(int fd, int event, PollCompleteCB cb = nullptr);

    /**
     * Return to get how many fd events are monitored
     */
    size_t fdCount() const;

    /**
     * Executes a task asynchronously
     * @param task The task to execute
     * @param may_sync If the calling thread is the polling thread of this object,
     *                  then if may_sync is true, the task will be executed synchronously
     * @return Whether the task was executed successfully (always returns true)
     */
    Task::Ptr async(TaskIn task, bool may_sync = true) override;

    /**
     * Similar to async, but adds the task to the head of the task queue,
     * giving it the highest priority
     * @param task The task to execute
     * @param may_sync If the calling thread is the polling thread of this object,
     *                  then if may_sync is true, the task will be executed synchronously
     * @return Whether the task was executed successfully (always returns true)
     */
    Task::Ptr async_first(TaskIn task, bool may_sync = true) override;

    /**
     * Checks if the thread calling this interface is the polling thread of this object
     * @return Whether the calling thread is the polling thread
     */
    bool isCurrentThread();

    /**
     * Delays the execution of a task
     * @param delay_ms The delay in milliseconds
     * @param task The task to execute, returns 0 to stop repeating the task,
     *              otherwise returns the delay for the next execution.
     *              If an exception is thrown in the task, it defaults to not repeating the task.
     * @return A cancellable task label
     */
    DelayTask::Ptr doDelayTask(uint64_t delay_ms, std::function<uint64_t()> task);

    /**
     * Gets the Poller instance associated with the current thread
     */
    static EventPoller::Ptr getCurrentPoller();

    /**
     * Gets the shared read buffer for all sockets in the current thread
     */
    SocketRecvBuffer::Ptr getSharedBuffer(bool is_udp);

    /**
     * Get the poller thread ID
     */
    std::thread::id getThreadId() const;

    /**
     * Get the thread name
     */
    const std::string &getThreadName() const;

private:
    /**
     * This object can only be constructed in EventPollerPool
     */
    EventPoller(std::string name);

    /**
     * Perform event polling
     * @param blocked Whether to execute polling with the thread that calls this interface
     * @param ref_self Whether to record this object to thread local variable
     */
    void runLoop(bool blocked, bool ref_self);

    /**
     * Internal pipe event, used to wake up the polling thread
     */
    void onPipeEvent(bool flush = false);

    /**
     * Switch threads and execute tasks
     * @param task
     * @param may_sync
     * @param first
     * @return The cancellable task itself, or nullptr if it has been executed synchronously
     */
    Task::Ptr async_l(TaskIn task, bool may_sync = true, bool first = false);

    /**
     * End event polling
     * Note that once ended, the polling thread cannot be resumed
     */
    void shutdown();

    /**
     * Refresh delayed tasks
     */
    int64_t flushDelayTask(uint64_t now);

    /**
     * Get the sleep time for select or epoll
     */
    int64_t getMinDelay();

    /**
     * Add pipe listening event
     */
    void addEventPipe();

private:
    class ExitException : public std::exception {};

private:
    // Mark the loop thread as exited
    bool _exit_flag;
    // Count how many fds are monitored
    size_t _fd_count = 0;
    // Thread name
    std::string _name;
    // Shared read buffer for all sockets under the current thread
    std::weak_ptr<SocketRecvBuffer> _shared_buffer[2];
    // Thread that executes the event loop
    std::thread *_loop_thread = nullptr;
    // Notify the event loop thread that it has started
    semaphore _sem_run_started;

    // Internal event pipe
    PipeWrap _pipe;
    // Tasks switched from other threads
    std::mutex _mtx_task;
    List<Task::Ptr> _list_task;

    // Keep the log available
    Logger::Ptr _logger;

#if defined(HAS_EPOLL) || defined(HAS_KQUEUE)
    // epoll and kqueue related
    epoll_fd _event_fd = INVALID_EVENT_FD;
    std::unordered_map<int, std::shared_ptr<PollEventCB>> _event_map;
#else
    // select related
    struct Poll_Record {
        using Ptr = std::shared_ptr<Poll_Record>;
        int fd;
        int event;
        int attach;
        PollEventCB call_back;
    };
    std::unordered_map<int, Poll_Record::Ptr> _event_map;
#endif // HAS_EPOLL
    std::unordered_set<int> _event_cache_expired;

    //Timer related
    std::multimap<uint64_t, DelayTask::Ptr> _delay_task_map;
};

class EventPollerPool : public std::enable_shared_from_this<EventPollerPool>, public TaskExecutorGetterImp {
public:
    using Ptr = std::shared_ptr<EventPollerPool>;
    static const std::string kOnStarted;
#define EventPollerPoolOnStartedArgs EventPollerPool &pool, size_t &size

    ~EventPollerPool() = default;

    /**
     * Get singleton
     * @return
     */
    static EventPollerPool &Instance();

    /**
     * Set the number of EventPoller instances, effective before the EventPollerPool singleton is created
     * If this method is not called, the default is to create thread::hardware_concurrency() EventPoller instances
     * @param size  Number of EventPoller instances, 0 means thread::hardware_concurrency()
     */
    static void setPoolSize(size_t size = 0);

    /**
     * Whether to set CPU affinity for internal thread creation, default is to set CPU affinity
     */
    static void enableCpuAffinity(bool enable);

    /**
     * Get the first instance
     * @return
     */
    EventPoller::Ptr getFirstPoller();

    /**
     * Get a lightly loaded instance based on the load
     * If prioritizing the current thread, it will return the current thread
     * The purpose of returning the current thread is to improve thread safety
     * @param prefer_current_thread Whether to prioritize getting the current thread
     */
    EventPoller::Ptr getPoller(bool prefer_current_thread = true);

    /**
     * Set whether getPoller() prioritizes returning the current thread
     * When creating Socket objects in batches, if prioritizing the current thread,
     * it will cause the load to be unbalanced, so it can be temporarily closed and then reopened
     * @param flag Whether to prioritize returning the current thread
     */
    void preferCurrentThread(bool flag = true);

private:
    EventPollerPool();

private:
    bool _prefer_current_thread = true;
};

} // namespace toolkit
#endif /* EventPoller_h */
