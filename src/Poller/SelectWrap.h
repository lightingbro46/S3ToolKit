/*
 * Copyright (c) 2025 The S3ToolKit project authors. All Rights Reserved.
 *
 * This file is part of S3ToolKit(https://github.com/S3MediaKit/S3ToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SRC_POLLER_SELECTWRAP_H_
#define SRC_POLLER_SELECTWRAP_H_

#include "Util/util.h"

namespace toolkit {

class FdSet {
public:
    FdSet();
    ~FdSet();
    void fdZero();
    void fdSet(int fd);
    void fdClr(int fd);
    bool isSet(int fd);
    void *_ptr;
};

int zl_select(int cnt, FdSet *read, FdSet *write, FdSet *err, struct timeval *tv);

} /* namespace toolkit */
#endif /* SRC_POLLER_SELECTWRAP_H_ */
