#ifndef UTIL_WORKTHREADPOOL_H_
#define UTIL_WORKTHREADPOOL_H_

#include <memory>
#include "Poller/EventPoller.h"

namespace toolkit {

class WorkThreadPool : public std::enable_shared_from_this<WorkThreadPool>, public TaskExecutorGetterImp {
public:
    using Ptr = std::shared_ptr<WorkThreadPool>;

    ~WorkThreadPool() override = default;

    /**
     * Get the singleton instance
     */
    static WorkThreadPool &Instance();

    /**
     * Set the number of EventPoller instances, effective before the WorkThreadPool singleton is created
     * If this method is not called, the default is to create thread::hardware_concurrency() EventPoller instances
     * @param size The number of EventPoller instances, if 0 then use thread::hardware_concurrency()
     */
    static void setPoolSize(size_t size = 0);

    /**
     * Whether to set CPU affinity when creating internal threads, CPU affinity is set by default
     */
    static void enableCpuAffinity(bool enable);

    /**
     * Get the first instance
     * @return
     */
    EventPoller::Ptr getFirstPoller();

    /**
     * Get a lightly loaded instance based on the load situation
     * If priority is given to the current thread, it will return the current thread
     * The purpose of returning the current thread is to improve thread safety
     * @return
     */
    EventPoller::Ptr getPoller();

protected:
    WorkThreadPool();
};

} /* namespace toolkit */
#endif /* UTIL_WORKTHREADPOOL_H_ */
