#ifndef UTIL_TIMETICKER_H_
#define UTIL_TIMETICKER_H_

#include <cassert>
#include "logger.h"

namespace toolkit {

class Ticker {
public:
    /**
     * This object can be used for code execution time statistics, and can be used for general timing
     * @param min_ms When the code execution time statistics is enabled, if the code execution time exceeds this parameter, a warning log is printed
     * @param ctx Log context capture, used to capture the current log code location
     * @param print_log Whether to print the code execution time
     */
    Ticker(uint64_t min_ms = 0,
           LogContextCapture ctx = LogContextCapture(Logger::Instance(), LWarn, __FILE__, "", __LINE__),
           bool print_log = false) : _ctx(std::move(ctx)) {
        if (!print_log) {
            _ctx.clear();
        }
        _created = _begin = getCurrentMillisecond();
        _min_ms = min_ms;
    }

    ~Ticker() {
        uint64_t tm = createdTime();
        if (tm > _min_ms) {
            _ctx << "take time: " << tm << "ms" << ", thread may be overloaded";
        } else {
            _ctx.clear();
        }
    }

    /**
     * Get the time from the last resetTime to now, in milliseconds
     */
    uint64_t elapsedTime() const {
        return getCurrentMillisecond() - _begin;
    }

    /**
     * Get the time from creation to now, in milliseconds
     */
    uint64_t createdTime() const {
        return getCurrentMillisecond() - _created;
    }

    /**
     * Reset the timer
     */
    void resetTime() {
        _begin = getCurrentMillisecond();
    }

private:
    uint64_t _min_ms;
    uint64_t _begin;
    uint64_t _created;
    LogContextCapture _ctx;
};

class SmoothTicker {
public:
    /**
     * This object is used to generate smooth timestamps
     * @param reset_ms Timestamp reset interval, every reset_ms milliseconds, the generated timestamp will be synchronized with the system timestamp
     */
    SmoothTicker(uint64_t reset_ms = 10000) {
        _reset_ms = reset_ms;
        _ticker.resetTime();
    }

    ~SmoothTicker() {}

    /**
     * Return a smooth timestamp, to prevent the timestamp from being unsmooth due to network jitter
     */
    uint64_t elapsedTime() {
        auto now_time = _ticker.elapsedTime();
        if (_first_time == 0) {
            if (now_time < _last_time) {
                auto last_time = _last_time - _time_inc;
                double elapse_time = (now_time - last_time);
                _time_inc += (elapse_time / ++_pkt_count) / 3;
                auto ret_time = last_time + _time_inc;
                _last_time = (uint64_t) ret_time;
                return (uint64_t) ret_time;
            }
            _first_time = now_time;
            _last_time = now_time;
            _pkt_count = 0;
            _time_inc = 0;
            return now_time;
        }

        auto elapse_time = (now_time - _first_time);
        _time_inc += elapse_time / ++_pkt_count;
        auto ret_time = _first_time + _time_inc;
        if (elapse_time > _reset_ms) {
            _first_time = 0;
        }
        _last_time = (uint64_t) ret_time;
        return (uint64_t) ret_time;
    }

    /**
     * Reset the timestamp to start from 0
     */
    void resetTime() {
        _first_time = 0;
        _pkt_count = 0;
        _ticker.resetTime();
    }

private:
    double _time_inc = 0;
    uint64_t _first_time = 0;
    uint64_t _last_time = 0;
    uint64_t _pkt_count = 0;
    uint64_t _reset_ms;
    Ticker _ticker;
};

#if !defined(NDEBUG)
#define TimeTicker() Ticker __ticker(5,WarnL,true)
#define TimeTicker1(tm) Ticker __ticker1(tm,WarnL,true)
#define TimeTicker2(tm, log) Ticker __ticker2(tm,log,true)
#else
#define TimeTicker()
#define TimeTicker1(tm)
#define TimeTicker2(tm,log)
#endif

} /* namespace toolkit */
#endif /* UTIL_TIMETICKER_H_ */
