#ifndef TCPSERVER_TCPSERVER_H
#define TCPSERVER_TCPSERVER_H

#include <memory>
#include <functional>
#include <unordered_map>
#include "Server.h"
#include "Session.h"
#include "Poller/Timer.h"
#include "Util/util.h"

namespace toolkit {

//Configurable TCP server; configuration is passed to the session object through the Session::attachServer method
class TcpServer : public Server {
public:
    using Ptr = std::shared_ptr<TcpServer>;

    /**
     * Creates a TCP server, the accept event of the listen fd will be added to all poller threads for listening
     * When calling the TcpServer::start function, multiple child TcpServer objects will be created internally,
     * These child TcpServer objects will be cloned through the Socket object in multiple poller threads to listen to the same listen fd
     * This way, the TCP server will distribute clients evenly across different poller threads through a preemptive accept approach
     * This approach can achieve client load balancing and improve connection acceptance speed
     */
    explicit TcpServer(const EventPoller::Ptr &poller = nullptr);
    ~TcpServer() override;

    /**
     * @brief Starts the TCP server
     * @param port Local port, 0 for random
     * @param host Listening network card IP
     * @param backlog TCP listen backlog
    */
    template <typename SessionType>
    void start(uint16_t port, const std::string &host = "::", uint32_t backlog = 1024, const std::function<void(std::shared_ptr<SessionType> &)> &cb = nullptr) {
        static std::string cls_name = toolkit::demangle(typeid(SessionType).name());
        //Session creator, creates different types of servers through it
        _session_alloc = [cb](const TcpServer::Ptr &server, const Socket::Ptr &sock) {
            auto session = std::shared_ptr<SessionType>(new SessionType(sock), [](SessionType *ptr) {
                TraceP(static_cast<Session *>(ptr)) << "~" << cls_name;
                delete ptr;
            });
            if (cb) {
                cb(session);
            }
            TraceP(static_cast<Session *>(session.get())) << cls_name;
            session->setOnCreateSocket(server->_on_create_socket);
            return std::make_shared<SessionHelper>(server, std::move(session), cls_name);
        };
        start_l(port, host, backlog);
    }

    /**
     * @brief Gets the server listening port number, the server can choose to listen on a random port
     */
    uint16_t getPort();

    /**
     * @brief Custom socket construction behavior
     */
    void setOnCreateSocket(Socket::onCreateSocket cb);

    /**
     * Creates a Session object based on the socket object
     * Ensures that this function is executed in the poller thread that owns the socket
     */
    Session::Ptr createSession(const Socket::Ptr &socket);

protected:
    virtual void cloneFrom(const TcpServer &that);
    virtual TcpServer::Ptr onCreatServer(const EventPoller::Ptr &poller);

    virtual Session::Ptr onAcceptConnection(const Socket::Ptr &sock);
    virtual Socket::Ptr onBeforeAcceptConnection(const EventPoller::Ptr &poller);

private:
    void onManagerSession();
    Socket::Ptr createSocket(const EventPoller::Ptr &poller);
    void start_l(uint16_t port, const std::string &host, uint32_t backlog);
    Ptr getServer(const EventPoller *) const;
    void setupEvent();

private:
    bool _multi_poller;
    bool _is_on_manager = false;
    bool _main_server = true;
    std::weak_ptr<TcpServer> _parent;
    Socket::Ptr _socket;
    std::shared_ptr<Timer> _timer;
    Socket::onCreateSocket _on_create_socket;
    std::unordered_map<SessionHelper *, SessionHelper::Ptr> _session_map;
    std::function<SessionHelper::Ptr(const TcpServer::Ptr &server, const Socket::Ptr &)> _session_alloc;
    std::unordered_map<const EventPoller *, Ptr> _cloned_server;
    //Object count statistics
    ObjectStatistic<TcpServer> _statistic;
};

} /* namespace toolkit */
#endif /* TCPSERVER_TCPSERVER_H */
