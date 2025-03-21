/*
 * Copyright (c) 2025 The S3ToolKit project authors. All Rights Reserved.
 *
 * This file is part of S3ToolKit(https://github.com/S3MediaKit/S3ToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef S3TOOLKIT_SERVER_H
#define S3TOOLKIT_SERVER_H

#include <unordered_map>
#include "Util/mini.h"
#include "Session.h"

namespace toolkit {

//Global Session record object, convenient for later management
//Thread-safe
class SessionMap : public std::enable_shared_from_this<SessionMap> {
public:
    friend class SessionHelper;
    using Ptr = std::shared_ptr<SessionMap>;

    //Singleton
    static SessionMap &Instance();
    ~SessionMap() = default;

    //Get Session
    Session::Ptr get(const std::string &tag);
    void for_each_session(const std::function<void(const std::string &id, const Session::Ptr &session)> &cb);

private:
    SessionMap() = default;

    //Remove Session
    bool del(const std::string &tag);
    //Add Session
    bool add(const std::string &tag, const Session::Ptr &session);

private:
    std::mutex _mtx_session;
    std::unordered_map<std::string, std::weak_ptr<Session> > _map_session;
};

class Server;

class SessionHelper {
public:
    bool enable = true;

    using Ptr = std::shared_ptr<SessionHelper>;

    SessionHelper(const std::weak_ptr<Server> &server, Session::Ptr session, std::string cls);
    ~SessionHelper();

    const Session::Ptr &session() const;
    const std::string &className() const;

private:
    std::string _cls;
    std::string _identifier;
    Session::Ptr _session;
    SessionMap::Ptr _session_map;
    std::weak_ptr<Server> _server;
};

//Server base class, temporarily only used to decouple SessionHelper from TcpServer
//Later, the common parts of TCP and UDP services will be added here.
class Server : public std::enable_shared_from_this<Server>, public mINI {
public:
    using Ptr = std::shared_ptr<Server>;

    explicit Server(EventPoller::Ptr poller = nullptr);
    virtual ~Server() = default;

protected:
    EventPoller::Ptr _poller;
};

} // namespace toolkit

#endif // S3TOOLKIT_SERVER_H