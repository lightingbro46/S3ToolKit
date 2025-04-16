#ifndef UTIL_RECYCLEPOOL_H_
#define UTIL_RECYCLEPOOL_H_

#include "List.h"
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace toolkit {

#if (defined(__GNUC__) && (__GNUC__ >= 5 || (__GNUC__ >= 4 && __GNUC_MINOR__ >= 9))) || defined(__clang__)             \
    || !defined(__GNUC__)
#define SUPPORT_DYNAMIC_TEMPLATE
#endif

template <typename C>
class ResourcePool_l;
template <typename C>
class ResourcePool;

template <typename C>
class shared_ptr_imp : public std::shared_ptr<C> {
public:
    shared_ptr_imp() {}

    /**
     * Constructs a smart pointer
     * @param ptr Raw pointer
     * @param weakPool Circular pool managing this pointer
     * @param quit Whether to give up circular reuse
     */
    shared_ptr_imp(
        C *ptr, const std::weak_ptr<ResourcePool_l<C>> &weakPool, std::shared_ptr<std::atomic_bool> quit,
        const std::function<void(C *)> &on_recycle);

    /**
     * Abandon or recover to continue using in the circular pool
     * @param flag
     */
    void quit(bool flag = true) {
        if (_quit) {
            *_quit = flag;
        }
    }

private:
    std::shared_ptr<std::atomic_bool> _quit;
};

template <typename C>
class ResourcePool_l : public std::enable_shared_from_this<ResourcePool_l<C>> {
public:
    using ValuePtr = shared_ptr_imp<C>;
    friend class shared_ptr_imp<C>;
    friend class ResourcePool<C>;

    ResourcePool_l() {
        _alloc = []() -> C * { return new C(); };
    }

#if defined(SUPPORT_DYNAMIC_TEMPLATE)
    template <typename... ArgTypes>
    ResourcePool_l(ArgTypes &&...args) {
        _alloc = [args...]() -> C * { return new C(args...); };
    }
#endif // defined(SUPPORT_DYNAMIC_TEMPLATE)

    ~ResourcePool_l() {
        for (auto ptr : _objs) {
            delete ptr;
        }
    }

    void setSize(size_t size) {
        _pool_size = size;
        _objs.reserve(size);
    }

    ValuePtr obtain(const std::function<void(C *)> &on_recycle = nullptr) {
        return ValuePtr(getPtr(), _weak_self, std::make_shared<std::atomic_bool>(false), on_recycle);
    }

    std::shared_ptr<C> obtain2() {
        auto weak_self = _weak_self;
        return std::shared_ptr<C>(getPtr(), [weak_self](C *ptr) {
            auto strongPool = weak_self.lock();
            if (strongPool) {
                //Put into circular pool
                strongPool->recycle(ptr);
            } else {
                delete ptr;
            }
        });
    }

private:
    void recycle(C *obj) {
        auto is_busy = _busy.test_and_set();
        if (!is_busy) {
            //Acquired lock
            if (_objs.size() >= _pool_size) {
                delete obj;
            } else {
                _objs.emplace_back(obj);
            }
            _busy.clear();
        } else {
            //Failed to acquire lock
            delete obj;
        }
    }

    C *getPtr() {
        C *ptr;
        auto is_busy = _busy.test_and_set();
        if (!is_busy) {
            //Acquired lock
            if (_objs.size() == 0) {
                ptr = _alloc();
            } else {
                ptr = _objs.back();
                _objs.pop_back();
            }
            _busy.clear();
        } else {
            //Failed to acquire lock
            ptr = _alloc();
        }
        return ptr;
    }

    void setup() { _weak_self = this->shared_from_this(); }

private:
    size_t _pool_size = 8;
    std::vector<C *> _objs;
    std::function<C *(void)> _alloc;
    std::atomic_flag _busy { false };
    std::weak_ptr<ResourcePool_l> _weak_self;
};

/**
 * Circular pool, note that objects in the circular pool cannot inherit from enable_shared_from_this!
 * @tparam C
 */
template <typename C>
class ResourcePool {
public:
    using ValuePtr = shared_ptr_imp<C>;
    ResourcePool() {
        pool.reset(new ResourcePool_l<C>());
        pool->setup();
    }
#if defined(SUPPORT_DYNAMIC_TEMPLATE)
    template <typename... ArgTypes>
    ResourcePool(ArgTypes &&...args) {
        pool = std::make_shared<ResourcePool_l<C>>(std::forward<ArgTypes>(args)...);
        pool->setup();
    }
#endif // defined(SUPPORT_DYNAMIC_TEMPLATE)
    void setSize(size_t size) { pool->setSize(size); }

    //Get an object, performance is slightly worse, but with more features
    ValuePtr obtain(const std::function<void(C *)> &on_recycle = nullptr) { return pool->obtain(on_recycle); }

    //Get an object, performance is slightly better
    std::shared_ptr<C> obtain2() { return pool->obtain2(); }

private:
    std::shared_ptr<ResourcePool_l<C>> pool;
};

template<typename C>
shared_ptr_imp<C>::shared_ptr_imp(C *ptr,
                                  const std::weak_ptr<ResourcePool_l<C> > &weakPool,
                                  std::shared_ptr<std::atomic_bool> quit,
                                  const std::function<void(C *)> &on_recycle) :
    std::shared_ptr<C>(ptr, [weakPool, quit, on_recycle](C *ptr) {
            if (on_recycle) {
                on_recycle(ptr);
            }
            auto strongPool = weakPool.lock();
            if (strongPool && !(*quit)) {
                //Loop pool is still in and does not give up putting into loop pool
                strongPool->recycle(ptr);
            } else {
                delete ptr;
            }
        }), _quit(std::move(quit)) {}

} /* namespace toolkit */
#endif /* UTIL_RECYCLEPOOL_H_ */
