#ifndef NETWORK_UDPCLIENT_H
#define NETWORK_UDPCLIENT_H

#include <memory>
#include "Socket.h"
#include "Util/SSLBox.h"
#include "Kcp.h"

namespace toolkit {

//Udp client, Socket object starts mutex lock by default
class UdpClient : public SocketHelper {
public:
    using Ptr = std::shared_ptr<UdpClient>;
    using OnRecvFrom = std::function<void(const Buffer::Ptr &buf, struct sockaddr *addr, int addr_len)>;
    using OnErr = std::function<void(const SockException &)>;

    UdpClient(const EventPoller::Ptr &poller = nullptr);
    ~UdpClient() override;

    /**
     * Start connecting to UDP server
     * @param peer_host Server IP or domain name
     * @param peer_port Server port
     * @param local_port Local port
     */
    virtual void startConnect(const std::string &peer_host, uint16_t peer_port, uint16_t local_port = 0);

    /**
     * Actively disconnect
     * @param ex Parameter when triggering onErr event
     */
    void shutdown(const SockException &ex = SockException(Err_shutdown, "self shutdown")) override;

    /**
     * Returns true when connecting or already connected, returns false when disconnected
     */
    virtual bool alive() const;

    /**
     * Set network adapter to use for communicating with server
     * @param local_ip Local network card IP
     */
    virtual void setNetAdapter(const std::string &local_ip);

    /**
     * Unique identifier
     */
    std::string getIdentifier() const override;

    void setOnRecvFrom(OnRecvFrom cb) {
        _on_recvfrom = std::move(cb);
    }

    void setOnError(OnErr cb) {
        _on_err = std::move(cb);
    }

protected:

    virtual void onRecvFrom(const Buffer::Ptr &buf, struct sockaddr *addr, int addr_len) {
        if (_on_recvfrom) {
            _on_recvfrom(buf, addr, addr_len);
        }
    }

    void onRecv(const Buffer::Ptr &buf) override {}

    void onError(const SockException &err) override {
        DebugL;
        if (_on_err) {
            _on_err(err);
        }
    }
 
    /**
     * This event is triggered every 2 seconds after UDP connection is successful
     */
    void onManager() override {}

private:
    mutable std::string _id;
    std::string _net_adapter = "::";
    std::shared_ptr<Timer> _timer;
    //Object count statistics
    ObjectStatistic<UdpClient> _statistic;

    OnRecvFrom _on_recvfrom;
    OnErr _on_err;
};

//Template object for implementing KCP client
template<typename UdpClientType>
class UdpClientWithKcp : public UdpClientType {
public:
    using Ptr = std::shared_ptr<UdpClientWithKcp>;

    template<typename ...ArgsType>
    UdpClientWithKcp(ArgsType &&...args)
        :UdpClientType(std::forward<ArgsType>(args)...) {
        _kcp_box = std::make_shared<KcpTransport>(false);
        _kcp_box->setOnWrite([&](const Buffer::Ptr &buf) { public_send(buf); });
        _kcp_box->setOnRead([&](const Buffer::Ptr &buf) { public_onRecv(buf); });
        _kcp_box->setOnErr([&](const SockException &ex) { public_onErr(ex); });
    }

    ~UdpClientWithKcp() override { }

    void onRecvFrom(const Buffer::Ptr &buf, struct sockaddr *addr, int addr_len) override {
        //KCP temporarily does not support one UDP Socket for multiple targets, so ignore addr parameter for now
        _kcp_box->input(buf);
    }

    ssize_t send(Buffer::Ptr buf) override {
        return _kcp_box->send(std::move(buf));
    }

    ssize_t sendto(Buffer::Ptr buf, struct sockaddr *addr = nullptr, socklen_t addr_len = 0) override {
        //KCP temporarily does not support one UDP Socket for multiple targets, so ignore addr parameter for now
        return _kcp_box->send(std::move(buf));
    }

    inline void public_onRecv(const Buffer::Ptr &buf) {
        //KCP temporarily does not support one UDP Socket for multiple targets, so always use the address parameter from bind
        UdpClientType::onRecvFrom(buf, (struct sockaddr*)&_peer_addr, _peer_addr_len);
    }

    inline void public_send(const Buffer::Ptr &buf) {
        UdpClientType::send(buf);
    }

    inline void public_onErr(const SockException &ex) { UdpClientWithKcp::onError(ex); }

    virtual void startConnect(const std::string &peer_host, uint16_t peer_port, uint16_t local_port = 0) override {
        _kcp_box->setPoller(UdpClientType::getPoller());
        _peer_addr = SockUtil::make_sockaddr(peer_host.data(), peer_port);
        _peer_addr_len = SockUtil::get_sock_len((const struct sockaddr*)&_peer_addr);
        UdpClientType::startConnect(peer_host, peer_port, local_port);
    }

    void setMtu(int mtu) {
        _kcp_box->setMtu(mtu);
    }

    void setInterval(int intervoal) {
        _kcp_box->setInterval(intervoal);
    }

    void setRxMinrto(int rx_minrto) {
        _kcp_box->setRxMinrto(rx_minrto);
    }

    void setWndSize(int sndwnd, int rcvwnd) {
        _kcp_box->setWndSize(sndwnd, rcvwnd);
    }

    void setDelayMode(KcpTransport::DelayMode delay_mode) {
        _kcp_box->setDelayMode(delay_mode);
    }

    void setFastResend(int resend) {
        _kcp_box->setFastResend(resend);
    }

    void setFastackConserve(bool flag) {
        _kcp_box->setFastackConserve(flag);
    }

    void setNoCwnd(bool flag) {
        _kcp_box->setNoCwnd(flag);
    }

    void setStreamMode(bool flag) {
        _kcp_box->setStreamMode(flag);
    }

private:
    struct sockaddr_storage _peer_addr;
    int _peer_addr_len = 0;
    KcpTransport::Ptr _kcp_box;
};

} /* namespace toolkit */
#endif /* NETWORK_UDPCLIENT_H */
