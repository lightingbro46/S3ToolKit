#include "Util/uv_errno.h"
#include "Util/onceToken.h"
#include "UdpServer.h"

using namespace std;

namespace toolkit {

static const uint8_t s_in6_addr_maped[]
    = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 };

static constexpr auto kUdpDelayCloseMS = 3 * 1000;

static UdpServer::PeerIdType makeSockId(sockaddr *addr, int) {
    UdpServer::PeerIdType ret;
    switch (addr->sa_family) {
        case AF_INET : {
            ret[0] = ((struct sockaddr_in *) addr)->sin_port >> 8;
            ret[1] = ((struct sockaddr_in *) addr)->sin_port & 0xFF;
            //Convert ipv4 addresses to ipv6 for unified processing
            memcpy(&ret[2], &s_in6_addr_maped, 12);
            memcpy(&ret[14], &(((struct sockaddr_in *) addr)->sin_addr), 4);
            return ret;
        }
        case AF_INET6 : {
            ret[0] = ((struct sockaddr_in6 *) addr)->sin6_port >> 8;
            ret[1] = ((struct sockaddr_in6 *) addr)->sin6_port & 0xFF;
            memcpy(&ret[2], &(((struct sockaddr_in6 *)addr)->sin6_addr), 16);
            return ret;
        }
        default: throw std::invalid_argument("invalid sockaddr address");
    }
}

UdpServer::UdpServer(const EventPoller::Ptr &poller) : Server(poller) {
    _multi_poller = !poller;
    setOnCreateSocket(nullptr);
}

void UdpServer::setupEvent() {
    _socket = createSocket(_poller);
    std::weak_ptr<UdpServer> weak_self = std::static_pointer_cast<UdpServer>(shared_from_this());
    _socket->setOnRead([weak_self](Buffer::Ptr &buf, struct sockaddr *addr, int addr_len) {
        if (auto strong_self = weak_self.lock()) {
            strong_self->onRead(buf, addr, addr_len);
        }
    });
}

UdpServer::~UdpServer() {
    if (!_cloned && _socket && _socket->rawFD() != -1) {
        InfoL << "Close udp server [" << _socket->get_local_ip() << "]: " << _socket->get_local_port();
    }
    _timer.reset();
    _socket.reset();
    _cloned_server.clear();
    if (!_cloned && _session_mutex && _session_map) {
        lock_guard<std::recursive_mutex> lck(*_session_mutex);
        _session_map->clear();
    }
}

void UdpServer::start_l(uint16_t port, const std::string &host) {
    setupEvent();
    //Only the main server creates a session map, other cloned servers share it
    _session_mutex = std::make_shared<std::recursive_mutex>();
    _session_map = std::make_shared<SessionMapType>();

    //Create a timer to manage these udp sessions periodically, these objects are only managed by the main server, cloned servers do not manage them
    std::weak_ptr<UdpServer> weak_self = std::static_pointer_cast<UdpServer>(shared_from_this());
    _timer = std::make_shared<Timer>(2.0f, [weak_self]() -> bool {
        if (auto strong_self = weak_self.lock()) {
            strong_self->onManagerSession();
            return true;
        }
        return false;
    }, _poller);

    if (_multi_poller) {
        //Clone the server to different threads to support multi-threading for the udp server
        EventPollerPool::Instance().for_each([&](const TaskExecutor::Ptr &executor) {
            auto poller = std::static_pointer_cast<EventPoller>(executor);
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

    if (!_socket->bindUdpSock(port, host.c_str())) {
        //Failed to bind udp port, possibly due to port occupation or permission issues
        std::string err = (StrPrinter << "Bind udp socket on " << host << " " << port << " failed: " << get_uv_errmsg(true));
        throw std::runtime_error(err);
    }

    for (auto &pr: _cloned_server) {
        //Start the child server
#if 0
        pr.second->_socket->cloneSocket(*_socket);
#else
        //Experiments have found that the cloneSocket method can save fd resources, but the thread drift problem is more serious on some systems
        pr.second->_socket->bindUdpSock(_socket->get_local_port(), _socket->get_local_ip());
#endif
    }
    InfoL << "UDP server bind to [" << host << "]: " << port;
}

UdpServer::Ptr UdpServer::onCreatServer(const EventPoller::Ptr &poller) {
    return Ptr(new UdpServer(poller), [poller](UdpServer *ptr) { poller->async([ptr]() { delete ptr; }); });
}

void UdpServer::cloneFrom(const UdpServer &that) {
    if (!that._socket) {
        throw std::invalid_argument("UdpServer::cloneFrom other with null socket");
    }
    setupEvent();
    _cloned = true;
    // clone callbacks
    _on_create_socket = that._on_create_socket;
    _session_alloc = that._session_alloc;
    _session_mutex = that._session_mutex;
    _session_map = that._session_map;
    // clone properties
    this->mINI::operator=(that);
}

void UdpServer::onRead(Buffer::Ptr &buf, sockaddr *addr, int addr_len) {
    const auto id = makeSockId(addr, addr_len);
    onRead_l(true, id, buf, addr, addr_len);
}

static void emitSessionRecv(const SessionHelper::Ptr &helper, const Buffer::Ptr &buf) {
    if (!helper->enable) {
        //Delayed destruction in progress
        return;
    }
    try {
        helper->session()->onRecv(buf);
    } catch (SockException &ex) {
        helper->session()->shutdown(ex);
    } catch (exception &ex) {
        helper->session()->shutdown(SockException(Err_shutdown, ex.what()));
    }
}

void UdpServer::onRead_l(bool is_server_fd, const UdpServer::PeerIdType &id, Buffer::Ptr &buf, sockaddr *addr, int addr_len) {
    //This function is triggered when the udp server fd receives data; in most cases, the data should be triggered by the peer fd, and this function should not be a hot spot
    bool is_new = false;
    if (auto helper = getOrCreateSession(id, buf, addr, addr_len, is_new)) {
        if (helper->session()->getPoller()->isCurrentThread()) {
            //The current thread receives data and processes it directly
            emitSessionRecv(helper, buf);
        } else {
            //Data migration to another thread requires switching threads first
            WarnL << "UDP packet incoming from other thread";
            std::weak_ptr<SessionHelper> weak_helper = helper;
            //Since the socket read buffer is shared and reused by all sockets on this thread, it cannot be used across threads and must be transferred first
            auto cacheable_buf = std::move(buf);
            helper->session()->async([weak_helper, cacheable_buf]() {
                if (auto strong_helper = weak_helper.lock()) {
                    emitSessionRecv(strong_helper, cacheable_buf);
                }
            });
        }

#if !defined(NDEBUG)
        if (!is_new) {
            TraceL << "UDP packet incoming from " << (is_server_fd ? "server fd" : "other peer fd");
        }
#endif
    }
}

void UdpServer::onManagerSession() {
    decltype(_session_map) copy_map;
    {
        std::lock_guard<std::recursive_mutex> lock(*_session_mutex);
        //Copy the map to prevent objects from being removed during traversal
        copy_map = std::make_shared<SessionMapType>(*_session_map);
    }
    auto lam = [copy_map]() {
        for (auto &pr : *copy_map) {
            auto &session = pr.second->session();
            if (!session->getPoller()->isCurrentThread()) {
                //This session does not belong to the management of this poller
                continue;
            }
            try {
                //UDP sessions need to handle timeouts
                session->onManager();
            } catch (exception &ex) {
                WarnL << "Exception occurred when emit onManager: " << ex.what();
            }
        }
    };
    if (_multi_poller){
        EventPollerPool::Instance().for_each([lam](const TaskExecutor::Ptr &executor) {
            std::static_pointer_cast<EventPoller>(executor)->async(lam);
        });
    } else {
        lam();
    }
}

SessionHelper::Ptr UdpServer::getOrCreateSession(const UdpServer::PeerIdType &id, Buffer::Ptr &buf, sockaddr *addr, int addr_len, bool &is_new) {
    {
        //Reduce the critical section
        std::lock_guard<std::recursive_mutex> lock(*_session_mutex);
        auto it = _session_map->find(id);
        if (it != _session_map->end()) {
            return it->second;
        }
    }
    is_new = true;
    return createSession(id, buf, addr, addr_len);
}

SessionHelper::Ptr UdpServer::createSession(const PeerIdType &id, Buffer::Ptr &buf, struct sockaddr *addr, int addr_len) {
    //Change to custom acquisition of poller objects to prevent load imbalance
    auto socket = createSocket(_multi_poller ? EventPollerPool::Instance().getPoller(false) : _poller, buf, addr, addr_len);
    if (!socket) {
        //Socket creation failed, the data received by this onRead event is discarded
        return nullptr;
    }

    auto addr_str = string((char *) addr, addr_len);
    std::weak_ptr<UdpServer> weak_self = std::static_pointer_cast<UdpServer>(shared_from_this());
    auto helper_creator = [this, weak_self, socket, addr_str, id]() -> SessionHelper::Ptr {
        auto server = weak_self.lock();
        if (!server) {
            return nullptr;
        }

        //If the UdpSession class corresponding to this client has already been created, return directly
        lock_guard<std::recursive_mutex> lck(*_session_mutex);
        auto it = _session_map->find(id);
        if (it != _session_map->end()) {
            return it->second;
        }

        assert(_socket);
        socket->bindUdpSock(_socket->get_local_port(), _socket->get_local_ip());
        socket->bindPeerAddr((struct sockaddr *) addr_str.data(), addr_str.size());

        auto helper = _session_alloc(server, socket);
        //Pass the configuration of this server to the Session
        helper->session()->attachServer(*this);

        std::weak_ptr<SessionHelper> weak_helper = helper;
        socket->setOnRead([weak_self, weak_helper, id](Buffer::Ptr &buf, struct sockaddr *addr, int addr_len) {
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return;
            }
            auto new_id = makeSockId(addr, addr_len);
            //Quickly determine if it's data for the current session, usually should be true
            if (id == new_id) {
                if (auto strong_helper = weak_helper.lock()) {
                    emitSessionRecv(strong_helper, buf);
                }
                return;
            }

            //Received data from a non-current peer fd, let the server dispatch this data to the appropriate session object
            strong_self->onRead_l(false, new_id, buf, addr, addr_len);
        });
        socket->setOnErr([weak_self, weak_helper, id](const SockException &err) {
            //Remove the session object when this function scope ends
            //The purpose is to ensure the onError function is executed before removing the session
            //And avoid not removing the session object when its onError function throws an exception
            onceToken token(nullptr, [&]() {
                //Remove the session
                auto strong_self = weak_self.lock();
                if (!strong_self) {
                    return;
                }
                //Delay removing the UDP session to prevent frequent and rapid object reconstruction
                strong_self->_poller->doDelayTask(kUdpDelayCloseMS, [weak_self, id]() {
                    if (auto strong_self = weak_self.lock()) {
                        //Remove the current session object from the shared map
                        lock_guard<std::recursive_mutex> lck(*strong_self->_session_mutex);
                        strong_self->_session_map->erase(id);
                    }
                    return 0;
                });
            });

            //Get a strong reference to the session
            if (auto strong_helper = weak_helper.lock()) {
                //Trigger the onError event callback
                TraceP(strong_helper->session()) << strong_helper->className() << " on err: " << err;
                strong_helper->enable = false;
                strong_helper->session()->onError(err);
            }
        });

        auto pr = _session_map->emplace(id, std::move(helper));
        assert(pr.second);
        return pr.first->second;
    };

    if (socket->getPoller()->isCurrentThread()) {
        //This socket is allocated in this thread, directly create a helper object
        return helper_creator();
    }

    //This socket is allocated in another thread, need to transfer the buffer first, then create a helper object in its thread and process the data
    auto cacheable_buf = std::move(buf);
    socket->getPoller()->async([helper_creator, cacheable_buf]() {
        //Create a helper object in the thread where the socket is located
        auto helper = helper_creator();
        if (helper) {
            //May not have actually created a helper object successfully, may have obtained a helper object created by another thread
            helper->session()->getPoller()->async([helper, cacheable_buf]() {
                //This data cannot be discarded, provided to the session object for consumption
                emitSessionRecv(helper, cacheable_buf);
            });
        }
    });
    return nullptr;
}

void UdpServer::setOnCreateSocket(onCreateSocket cb) {
    if (cb) {
        _on_create_socket = std::move(cb);
    } else {
        _on_create_socket = [](const EventPoller::Ptr &poller, const Buffer::Ptr &buf, struct sockaddr *addr, int addr_len) {
            return Socket::createSocket(poller, false);
        };
    }
    for (auto &pr : _cloned_server) {
        pr.second->setOnCreateSocket(cb);
    }
}

uint16_t UdpServer::getPort() {
    if (!_socket) {
        return 0;
    }
    return _socket->get_local_port();
}

Socket::Ptr UdpServer::createSocket(const EventPoller::Ptr &poller, const Buffer::Ptr &buf, struct sockaddr *addr, int addr_len) {
    return _on_create_socket(poller, buf, addr, addr_len);
}


StatisticImp(UdpServer)

} // namespace toolkit
