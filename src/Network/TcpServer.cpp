#include "TcpServer.h"
#include "Util/uv_errno.h"
#include "Util/onceToken.h"

using namespace std;

namespace toolkit {

INSTANCE_IMP(SessionMap)
StatisticImp(TcpServer)

TcpServer::TcpServer(const EventPoller::Ptr &poller) : Server(poller) {
    _multi_poller = !poller;
    setOnCreateSocket(nullptr);
}

void TcpServer::setupEvent() {
    _socket = createSocket(_poller);
    weak_ptr<TcpServer> weak_self = std::static_pointer_cast<TcpServer>(shared_from_this());
#if 1
    _socket->setOnBeforeAccept([weak_self](const EventPoller::Ptr &poller) -> Socket::Ptr {
        if (auto strong_self = weak_self.lock()) {
            return strong_self->onBeforeAcceptConnection(poller);
        }
        return nullptr;
    });
    _socket->setOnAccept([weak_self](Socket::Ptr &sock, shared_ptr<void> &complete) {
        if (auto strong_self = weak_self.lock()) {
            auto ptr = sock->getPoller().get();
            auto server = strong_self->getServer(ptr);
            ptr->async([server, sock, complete]() {
                //This TCP client is dispatched to the corresponding thread of the TcpServer server
                server->onAcceptConnection(sock);
            });
        }
    });
#else
    _socket->setOnAccept([weak_self](Socket::Ptr &sock, shared_ptr<void> &complete) {
        if (auto strong_self = weak_self.lock()) {
            if (strong_self->_multi_poller) {
                EventPollerPool::Instance().getExecutor([sock, complete, weak_self](const TaskExecutor::Ptr &exe) {
                    if (auto strong_self = weak_self.lock()) {
                        sock->moveTo(static_pointer_cast<EventPoller>(exe));
                        strong_self->getServer(sock->getPoller().get())->onAcceptConnection(sock);
                    }
                });
            } else {
                strong_self->onAcceptConnection(sock);
            }
        }
    });
#endif
}

TcpServer::~TcpServer() {
    if (_main_server && _socket && _socket->rawFD() != -1) {
        InfoL << "Close tcp server [" << _socket->get_local_ip() << "]: " << _socket->get_local_port();
    }
    _timer.reset();
    //First close the socket listening to prevent receiving new connections
    _socket.reset();
    _session_map.clear();
    _cloned_server.clear();
}

uint16_t TcpServer::getPort() {
    if (!_socket) {
        return 0;
    }
    return _socket->get_local_port();
}

void TcpServer::setOnCreateSocket(Socket::onCreateSocket cb) {
    if (cb) {
        _on_create_socket = std::move(cb);
    } else {
        _on_create_socket = [](const EventPoller::Ptr &poller) {
            return Socket::createSocket(poller, false);
        };
    }
    for (auto &pr : _cloned_server) {
        pr.second->setOnCreateSocket(cb);
    }
}

TcpServer::Ptr TcpServer::onCreatServer(const EventPoller::Ptr &poller) {
    return Ptr(new TcpServer(poller), [poller](TcpServer *ptr) { poller->async([ptr]() { delete ptr; }); });
}

Socket::Ptr TcpServer::onBeforeAcceptConnection(const EventPoller::Ptr &poller) {
    assert(_poller->isCurrentThread());
    //Modify this to a custom way of getting the poller object to prevent load imbalance
    return createSocket(_multi_poller ? EventPollerPool::Instance().getPoller(false) : _poller);
}

void TcpServer::cloneFrom(const TcpServer &that) {
    if (!that._socket) {
        throw std::invalid_argument("TcpServer::cloneFrom other with null socket");
    }
    setupEvent();
    _main_server = false;
    _on_create_socket = that._on_create_socket;
    _session_alloc = that._session_alloc;
    _multi_poller = that._multi_poller;
    weak_ptr<TcpServer> weak_self = std::static_pointer_cast<TcpServer>(shared_from_this());
    _timer = std::make_shared<Timer>(2.0f, [weak_self]() -> bool {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        strong_self->onManagerSession();
        return true;
    }, _poller);
    this->mINI::operator=(that);
    _parent = static_pointer_cast<TcpServer>(const_cast<TcpServer &>(that).shared_from_this());
}

//Received a client connection request
Session::Ptr TcpServer::onAcceptConnection(const Socket::Ptr &sock) {
    assert(_poller->isCurrentThread());
    weak_ptr<TcpServer> weak_self = std::static_pointer_cast<TcpServer>(shared_from_this());
    //Create a Session; here implement creating different service session instances
    auto helper = _session_alloc(std::static_pointer_cast<TcpServer>(shared_from_this()), sock);
    auto session = helper->session();
    //Pass the configuration of this server to the Session
    session->attachServer(*this);

    //_session_map::emplace will definitely succeed
    auto success = _session_map.emplace(helper.get(), helper).second;
    assert(success == true);

    weak_ptr<Session> weak_session = session;
    //Session receives data event
    sock->setOnRead([weak_session](const Buffer::Ptr &buf, struct sockaddr *, int) {
        //Get the strong application of the session
        auto strong_session = weak_session.lock();
        if (!strong_session) {
            return;
        }
        try {
            strong_session->onRecv(buf);
        } catch (SockException &ex) {
            strong_session->shutdown(ex);
        } catch (exception &ex) {
            strong_session->shutdown(SockException(Err_shutdown, ex.what()));
        }
    });

    SessionHelper *ptr = helper.get();
    auto cls = ptr->className();
    //Session receives an error event
    sock->setOnErr([weak_self, weak_session, ptr, cls](const SockException &err) {
        //Remove the session object when the function scope ends
        //The purpose is to ensure that the onError function is executed before removing the session
        //And avoid not removing the session object when the onError function throws an exception
        onceToken token(nullptr, [&]() {
            //Remove the session
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return;
            }

            assert(strong_self->_poller->isCurrentThread());
            if (!strong_self->_is_on_manager) {
                //This event is not triggered by onManager, directly operate on the map
                strong_self->_session_map.erase(ptr);
            } else {
                //Cannot directly delete elements when traversing the map
                strong_self->_poller->async([weak_self, ptr]() {
                    auto strong_self = weak_self.lock();
                    if (strong_self) {
                        strong_self->_session_map.erase(ptr);
                    }
                }, false);
            }
        });

        //Get the strong reference of the session
        auto strong_session = weak_session.lock();
        if (strong_session) {
            //Trigger the onError event callback
            TraceP(strong_session) << cls << " on err: " << err;
            strong_session->onError(err);
        }
    });
    return session;
}

void TcpServer::start_l(uint16_t port, const std::string &host, uint32_t backlog) {
    setupEvent();

    //Create a new timer to manage these TCP sessions periodically
    weak_ptr<TcpServer> weak_self = std::static_pointer_cast<TcpServer>(shared_from_this());
    _timer = std::make_shared<Timer>(2.0f, [weak_self]() -> bool {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        strong_self->onManagerSession();
        return true;
    }, _poller);

    if (_multi_poller) {
        EventPollerPool::Instance().for_each([&](const TaskExecutor::Ptr &executor) {
            EventPoller::Ptr poller = static_pointer_cast<EventPoller>(executor);
            if (poller == _poller) {
                return;
            }
            auto &serverRef = _cloned_server[poller.get()];
            if (!serverRef) {
                serverRef = onCreatServer(poller);
            }
            if (serverRef) {
                serverRef->cloneFrom(*this);
            }
        });
    }

    if (!_socket->listen(port, host.c_str(), backlog)) {
        //TCP listener creation failed, possibly due to port occupation or permission issues
        string err = (StrPrinter << "Listen on " << host << " " << port << " failed: " << get_uv_errmsg(true));
        throw std::runtime_error(err);
    }
    for (auto &pr: _cloned_server) {
        //Start the child Server
        pr.second->_socket->cloneSocket(*_socket);
    }

    InfoL << "TCP server listening on [" << host << "]: " << port;
}

void TcpServer::onManagerSession() {
    assert(_poller->isCurrentThread());

    onceToken token([&]() {
        _is_on_manager = true;
    }, [&]() {
        _is_on_manager = false;
    });

    for (auto &pr : _session_map) {
        //When traversing, the onErr event may be triggered (also operates on _session_map)
        try {
            pr.second->session()->onManager();
        } catch (exception &ex) {
            WarnL << ex.what();
        }
    }
}

Socket::Ptr TcpServer::createSocket(const EventPoller::Ptr &poller) {
    return _on_create_socket(poller);
}

TcpServer::Ptr TcpServer::getServer(const EventPoller *poller) const {
    auto parent = _parent.lock();
    auto &ref = parent ? parent->_cloned_server : _cloned_server;
    auto it = ref.find(poller);
    if (it != ref.end()) {
        //Dispatch to the cloned server
        return it->second;
    }
    //Dispatch to the parent server
    return static_pointer_cast<TcpServer>(parent ? parent : const_cast<TcpServer *>(this)->shared_from_this());
}

Session::Ptr TcpServer::createSession(const Socket::Ptr &sock) {
    return getServer(sock->getPoller().get())->onAcceptConnection(sock);
}

} /* namespace toolkit */

