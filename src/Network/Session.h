/*
 * Copyright (c) 2025 The S3ToolKit project authors. All Rights Reserved.
 *
 * This file is part of S3ToolKit(https://github.com/S3MediaKit/S3ToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef S3TOOLKIT_SESSION_H
#define S3TOOLKIT_SESSION_H

#include <memory>
#include "Socket.h"
#include "Util/util.h"
#include "Util/SSLBox.h"

namespace toolkit {

//Session, used to store the relationship between a client and a server
class Server;
class TcpSession;
class UdpSession;

class Session : public SocketHelper {
public:
    using Ptr = std::shared_ptr<Session>;

    Session(const Socket::Ptr &sock);
    ~Session() override = default;

    /**
     * After creating a Session, the Server will pass its configuration parameters to the Session through this function
     * @param server, server object
     */
    virtual void attachServer(const Server &server) {}

    /**
     * As the unique identifier of this Session
     * @return unique identifier
     */
    std::string getIdentifier() const override;

private:
    mutable std::string _id;
    std::unique_ptr<toolkit::ObjectStatistic<toolkit::TcpSession> > _statistic_tcp;
    std::unique_ptr<toolkit::ObjectStatistic<toolkit::UdpSession> > _statistic_udp;
};

//This template allows the TCP server to quickly support TLS
template <typename SessionType>
class SessionWithSSL : public SessionType {
public:
    template <typename... ArgsType>
    SessionWithSSL(ArgsType &&...args)
        : SessionType(std::forward<ArgsType>(args)...) {
        _ssl_box.setOnEncData([&](const Buffer::Ptr &buf) { public_send(buf); });
        _ssl_box.setOnDecData([&](const Buffer::Ptr &buf) { public_onRecv(buf); });
    }

    ~SessionWithSSL() override { _ssl_box.flush(); }

    void onRecv(const Buffer::Ptr &buf) override { _ssl_box.onRecv(buf); }

    //Adding public_onRecv and public_send functions is to solve a bug in lower versions of gcc where a lambda cannot access protected or private methods
    inline void public_onRecv(const Buffer::Ptr &buf) { SessionType::onRecv(buf); }
    inline void public_send(const Buffer::Ptr &buf) { SessionType::send(buf); }

    bool overSsl() const override { return true; }

protected:
    ssize_t send(Buffer::Ptr buf) override {
        auto size = buf->size();
        _ssl_box.onSend(std::move(buf));
        return size;
    }

private:
    SSL_Box _ssl_box;
};

} // namespace toolkit

#endif // S3TOOLKIT_SESSION_H