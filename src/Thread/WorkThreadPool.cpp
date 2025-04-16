#include "WorkThreadPool.h"

namespace toolkit {

static size_t s_pool_size = 0;
static bool s_enable_cpu_affinity = true;

INSTANCE_IMP(WorkThreadPool)

EventPoller::Ptr WorkThreadPool::getFirstPoller() {
    return std::static_pointer_cast<EventPoller>(_threads.front());
}

EventPoller::Ptr WorkThreadPool::getPoller() {
    return std::static_pointer_cast<EventPoller>(getExecutor());
}

WorkThreadPool::WorkThreadPool() {
    //Lowest priority
    addPoller("work poller", s_pool_size, ThreadPool::PRIORITY_LOWEST, false, s_enable_cpu_affinity);
}

void WorkThreadPool::setPoolSize(size_t size) {
    s_pool_size = size;
}

void WorkThreadPool::enableCpuAffinity(bool enable) {
    s_enable_cpu_affinity = enable;
}

} /* namespace toolkit */

