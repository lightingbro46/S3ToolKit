#ifndef SEMAPHORE_H_
#define SEMAPHORE_H_

#include <mutex>
#include <chrono>
#include <condition_variable>

namespace toolkit {

class semaphore {
public:
    explicit semaphore(size_t initial = 0) {
#if defined(HAVE_SEM)
        sem_init(&_sem, 0, initial);
#else
        _count = initial;
#endif
    }

    ~semaphore() {
#if defined(HAVE_SEM)
        sem_destroy(&_sem);
#endif
    }

    void post(size_t n = 1) {
#if defined(HAVE_SEM)
        while (n--) {
            sem_post(&_sem);
        }
#else
        std::unique_lock<std::recursive_mutex> lock(_mutex);
        _count += n;
        if (n == 1) {
            _condition.notify_one();
        } else {
            _condition.notify_all();
        }
#endif
    }

    void wait() {
#if defined(HAVE_SEM)
        sem_wait(&_sem);
#else
        std::unique_lock<std::recursive_mutex> lock(_mutex);
        while (_count == 0) {
            _condition.wait(lock);
        }
        --_count;
#endif
    }

    bool wait(unsigned int timeout_ms) {
#if defined(HAVE_SEM)
        struct timespec ts;
        // Get current time
        if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
            perror("clock_gettime failed");
            return -1;
        }
        // Add timeout to current time to get absolute time
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec = ts.tv_nsec % 1000000000;
        }
        sem_timedwait(&_sem, &ts);

        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
            return false;
        }

        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec = ts.tv_nsec % 1000000000;
        }

        int result = sem_timedwait(&_sem, &ts);
        return result == 0; // Returns true on success, false on timeout/failure
#else

        std::unique_lock<std::recursive_mutex> lock(_mutex);
        auto now = std::chrono::system_clock::now();
        auto waitTime = now + std::chrono::milliseconds(timeout_ms);

        bool success = _condition.wait_until(lock, waitTime, [this] { return _count > 0; });

        if (success) {
            --_count;
        }
        return success;
#endif
    }

private:
#if defined(HAVE_SEM)
    sem_t _sem;
#else
    size_t _count;
    std::recursive_mutex _mutex;
    std::condition_variable_any _condition;
#endif
};

} /* namespace toolkit */
#endif /* SEMAPHORE_H_ */
