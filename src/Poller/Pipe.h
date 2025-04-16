#ifndef Pipe_h
#define Pipe_h

#include <functional>
#include "PipeWrap.h"
#include "EventPoller.h"

namespace toolkit {

class Pipe {
public:
    using onRead = std::function<void(int size, const char *buf)>;

    Pipe(const onRead &cb = nullptr, const EventPoller::Ptr &poller = nullptr);
    ~Pipe();

    void send(const char *send, int size = 0);

private:
    std::shared_ptr<PipeWrap> _pipe;
    EventPoller::Ptr _poller;
};

}  // namespace toolkit
#endif /* Pipe_h */