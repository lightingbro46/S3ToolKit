#ifndef NETWORK_TCPCLIENT_H
#define NETWORK_TCPCLIENT_H

#include <memory>
#include "Socket.h"
#include "Util/SSLBox.h"

namespace toolkit {

//Tcp client, Socket object defaults to starting mutex lock
class TcpClient : public SocketHelper {
public:
    using Ptr = std::shared_ptr<TcpClient>;
    TcpClient(const EventPoller::Ptr &poller = nullptr);
    ~TcpClient() override;

    /**
     * Start connecting to the TCP server
     * @param url Server IP or domain name
     * @param port Server port
     * @param timeout_sec Timeout time, in seconds
     * @param local_port Local port
     */
    virtual void startConnect(const std::string &url, uint16_t port, float timeout_sec = 5, uint16_t local_port = 0);
    
    /**
     * Start connecting to the TCP server through a proxy
     * @param url Server IP or domain name
     * @proxy_host Proxy IP
     * @proxy_port Proxy port
     * @param timeout_sec Timeout time, in seconds
     * @param local_port Local port
     */
    virtual void startConnectWithProxy(const std::string &url, const std::string &proxy_host, uint16_t proxy_port, float timeout_sec = 5, uint16_t local_port = 0){};
    
    /**
     * Actively disconnect the connection
     * @param ex Parameter when triggering the onErr event
     */
    void shutdown(const SockException &ex = SockException(Err_shutdown, "self shutdown")) override;

    /**
     * Returns true if connected or connecting, returns false if disconnected
     */
    virtual bool alive() const;

    /**
     * Set the network card adapter, use this network card to communicate with the server
     * @param local_ip Local network card IP
     */
    virtual void setNetAdapter(const std::string &local_ip);

    /**
     * Unique identifier
     */
    std::string getIdentifier() const override;

    size_t getSendSpeed() const;

    size_t getRecvSpeed() const;

    size_t getRecvTotalBytes() const;

    size_t getSendTotalBytes() const;

protected:
    /**
     * Connection result callback
     * @param ex Success or failure
     */
    virtual void onConnect(const SockException &ex) = 0;

    /**
     * Trigger this event every 2 seconds after a successful TCP connection
     */
    void onManager() override {}

private:
    void onSockConnect(const SockException &ex);

private:
    mutable std::string _id;
    std::string _net_adapter = "::";
    std::shared_ptr<Timer> _timer;
    //Object count statistics
    ObjectStatistic<TcpClient> _statistic;
};

//Template object for implementing TLS client
template<typename TcpClientType>
class TcpClientWithSSL : public TcpClientType {
public:
    using Ptr = std::shared_ptr<TcpClientWithSSL>;

    template<typename ...ArgsType>
    TcpClientWithSSL(ArgsType &&...args):TcpClientType(std::forward<ArgsType>(args)...) {}

    ~TcpClientWithSSL() override {
        if (_ssl_box) {
            _ssl_box->flush();
        }
    }

    void onRecv(const Buffer::Ptr &buf) override {
        if (_ssl_box) {
            _ssl_box->onRecv(buf);
        } else {
            TcpClientType::onRecv(buf);
        }
    }

    //Enable other unoverridden send functions
    using TcpClientType::send;

    ssize_t send(Buffer::Ptr buf) override {
        if (_ssl_box) {
            auto size = buf->size();
            _ssl_box->onSend(std::move(buf));
            return size;
        }
        return TcpClientType::send(std::move(buf));
    }

    //Adding public_onRecv and public_send functions is to solve a bug in lower version gcc where a lambda cannot access protected or private methods
    inline void public_onRecv(const Buffer::Ptr &buf) {
        TcpClientType::onRecv(buf);
    }

    inline void public_send(const Buffer::Ptr &buf) {
        TcpClientType::send(buf);
    }

    void startConnect(const std::string &url, uint16_t port, float timeout_sec = 5, uint16_t local_port = 0) override {
        _host = url;
        TcpClientType::startConnect(url, port, timeout_sec, local_port);
    }
    void startConnectWithProxy(const std::string &url, const std::string &proxy_host, uint16_t proxy_port, float timeout_sec = 5, uint16_t local_port = 0) override {
        _host = url;
        TcpClientType::startConnect(proxy_host, proxy_port, timeout_sec, local_port);
    }

    bool overSsl() const override { return (bool)_ssl_box; }

protected:
    void onConnect(const SockException &ex) override {
        if (!ex) {
            _ssl_box = std::make_shared<SSL_Box>(false);
            _ssl_box->setOnDecData([this](const Buffer::Ptr &buf) {
                public_onRecv(buf);
            });
            _ssl_box->setOnEncData([this](const Buffer::Ptr &buf) {
                public_send(buf);
            });

            if (!isIP(_host.data())) {
                //Set ssl domain
                _ssl_box->setHost(_host.data());
            }
        }
        TcpClientType::onConnect(ex);
    }
    /**
     * Reset ssl, mainly to solve some 302 redirects when switching between http and https
     */
    void setDoNotUseSSL() {
        _ssl_box.reset();
    }
private:
    std::string _host;
    std::shared_ptr<SSL_Box> _ssl_box;
};

} /* namespace toolkit */
#endif /* NETWORK_TCPCLIENT_H */
