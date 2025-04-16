#ifndef SPEED_STATISTIC_H_
#define SPEED_STATISTIC_H_

#include "TimeTicker.h"

namespace toolkit {

class BytesSpeed {
public:
    BytesSpeed() = default;
    ~BytesSpeed() = default;

    /**
     * Add statistical bytes
     */
    BytesSpeed &operator+=(size_t bytes) {
        _bytes += bytes;
        if (_bytes > 1024 * 1024) {
            //Data greater than 1MB is calculated once for network speed
            computeSpeed();
        }
        return *this;
    }

    /**
     * Get speed, unit bytes/s
     */
    int getSpeed() {
        if (_ticker.elapsedTime() < 1000) {
            //Get frequency less than 1 second, return the last calculation result
            return _speed;
        }
        return computeSpeed();
    }

private:
    int computeSpeed() {
        auto elapsed = _ticker.elapsedTime();
        if (!elapsed) {
            return _speed;
        }
        _speed = (int)(_bytes * 1000 / elapsed);
        _ticker.resetTime();
        _bytes = 0;
        return _speed;
    }

private:
    int _speed = 0;
    size_t _bytes = 0;
    Ticker _ticker;
};

} /* namespace toolkit */
#endif /* SPEED_STATISTIC_H_ */
