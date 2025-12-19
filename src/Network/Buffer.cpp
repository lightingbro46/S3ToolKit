#include <cstdlib>
#include "Buffer.h"
#include "Util/onceToken.h"

namespace toolkit {

StatisticImp(Buffer)
StatisticImp(BufferRaw)
StatisticImp(BufferLikeString)

BufferRaw::Ptr BufferRaw::create(size_t size) {
#if 0
    static ResourcePool<BufferRaw> packet_pool;
    static onceToken token([]() {
        packet_pool.setSize(1024);
    });
    auto ret = packet_pool.obtain2();
    ret->setSize(0);
    return ret;
#else
    return Ptr(new BufferRaw(size));
#endif
}

}//namespace toolkit
