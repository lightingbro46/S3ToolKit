#ifndef TOOLKIT_NETWORK_KCP_H
#define TOOLKIT_NETWORK_KCP_H

#include "Network/Buffer.h"
#include "Network/sockutil.h"
#include "Poller/EventPoller.h"
#include "Poller/Timer.h"
#include "Util/TimeTicker.h"
#include "Socket.h"

namespace toolkit {

class KcpHeader {
public:
    static const size_t HEADER_SIZE = 24;

    enum class Cmd : uint8_t {
        CMD_PUSH = 81,  // cmd: push data
        CMD_ACK  = 82,  // cmd: ack
        CMD_WASK = 83,  // cmd: window probe (ask)
        CMD_WINS = 84,  // cmd: window size (tell)
    };

    uint32_t _conv;     // Session ID, used to identify a session
    Cmd      _cmd;      // Command field, used to identify the type of data packet
    uint8_t  _frg = 0;  // Fragment number, used for message fragmentation, 0 indicates the last fragment
    uint16_t _wnd;      // Receive window size
    uint32_t _ts;       // Timestamp, 2^32ms, about 49.7 days will overflow once
    uint32_t _sn;       // Sequence number
    uint32_t _una;      // The first unacknowledged packet sequence number to be received
    uint32_t _len = 0;  // Length of the payload data (excluding header length)

public:

    // Getters for KcpHeader members
    uint32_t getConv() const { return _conv; }
    Cmd getCmd() const { return _cmd; }
    uint8_t getFrg() const { return _frg; }
    uint16_t getWnd() const { return _wnd; }
    uint32_t getTs() const { return _ts; }
    uint32_t getSn() const { return _sn; }
    uint32_t getUna() const { return _una; }
    uint32_t getLen() const { return _len; }

    // Setters for KcpHeader members
    void setConv(uint32_t conv) { _conv = conv; }
    void setCmd(Cmd cmd) { _cmd = cmd; }
    void setFrg(uint8_t frg) { _frg = frg; }
    void setWnd(uint16_t wnd) { _wnd = wnd; }
    void setTs(uint32_t ts) { _ts = ts; }
    void setSn(uint32_t sn) { _sn = sn; }
    void setUna(uint32_t una) { _una = una; }
    void setLen(uint32_t len) { _len = len; }

    uint32_t getPacketSize() const { return _len + HEADER_SIZE; }
    bool loadHeaderFromData(const char *data, size_t len);
    bool storeHeaderToData(char *buf, size_t size);
};

class KcpPacket : public KcpHeader, public toolkit::BufferRaw {
public:
    using Ptr = std::shared_ptr<KcpPacket>;

    static KcpPacket::Ptr parse(const char* data, size_t len);

    KcpPacket() {};
    KcpPacket(uint32_t conv, Cmd cmd, size_t payloadSize) {
        setConv(conv);
        setCmd(cmd);
        setPayLoadSize(payloadSize);
    };

    KcpPacket(size_t payloadSize) {
        setPayLoadSize(payloadSize);
    }

    virtual ~KcpPacket();

    bool storeToData();

    char *getPayloadData() {
        return data() + HEADER_SIZE;
    };

    uint32_t getResendts() const { return _resendts; }
    uint32_t getRto() const { return _rto; }
    uint32_t getFastack() const { return _fastack; }
    uint32_t getXmit() const { return _xmit; }

    void setResendts(uint32_t resendts) { _resendts = resendts; }
    void setRto(uint32_t rto) {_rto = rto; }
    void setFastack(uint32_t fastack) { _fastack = fastack; }
    void setXmit(uint32_t xmit) { _xmit = xmit; }

    void setPayLoadSize(size_t len) {
        setCapacity(len + HEADER_SIZE + 1);
        setSize(len + HEADER_SIZE);
        setLen(len);
    }

protected:
    bool loadFromData(const char *data, size_t len);

private:
    uint32_t _resendts; // Retransmission timeout timestamp, indicating the next retransmission time of this packet
    uint32_t _rto;      // Retransmission timeout, indicating how long the packet will be retransmitted if no ACK is received, dynamically adjusted based on RTT
    uint32_t _fastack;  // Fast acknowledgment counter
    uint32_t _xmit;     // Transmission count, used to count retransmissions
};

// Data packet
class KcpDataPacket : public KcpPacket {
public:
    KcpDataPacket(uint32_t conv, size_t payloadSize)
    : KcpPacket(conv, KcpHeader::Cmd::CMD_WASK, payloadSize) {
    }
};

// ACK packet
class KcpAckPacket : public KcpPacket {
public:
    KcpAckPacket(uint32_t conv) 
    : KcpPacket(conv, KcpHeader::Cmd::CMD_ACK, 0) {
    }
};

// Probe window size packet
class KcpProbePacket : public KcpPacket {
public:
    KcpProbePacket(uint32_t conv)
    : KcpPacket(conv, KcpHeader::Cmd::CMD_WASK, 0) {
    }

};

// Tell window size packet
class KcpTellPacket : public KcpPacket {
public:
    KcpTellPacket(uint32_t conv)
    : KcpPacket(conv, KcpHeader::Cmd::CMD_WINS, 0) {
    }
};

//Parameters can be adjusted according to actual needs
//Reference kcp V.1.7 implementation with the following recommended modes and parameters
//Default, flow control enabled: setDelayMode(DELAY_MODE_NORMAL); setInterval(10); setFastResend(0); setNoCwnd(false)
//Normal, flow control disabled: setDelayMode(DELAY_MODE_NORMAL); setInterval(10); setFastResend(0); setNoCwnd(true)
//Fast, flow control disabled: setDelayMode(DELAY_MODE_NO_DELAY); setInterval(10); setFastResend(1); setNoCwnd(true); setRxMinrto(10)
class KcpTransport : public std::enable_shared_from_this<KcpTransport> {
public:
    using Ptr = std::shared_ptr<KcpTransport>;

    enum DelayMode {
        DELAY_MODE_NORMAL   = 0,    // In normal mode, RTO is doubled every time it is retransmitted, and the minimum RTO is increased by 12.5%.
        DELAY_MODE_FAST     = 1,    // In fast mode, RTO is increased by half of the current packet's RTO each time it is retransmitted, without additional delay
        DELAY_MODE_NO_DELAY = 2,    // In no-delay mode, RTO is increased by half of the base RTO each time it is retransmitted, without additional delay
    };

    static const uint32_t IKCP_ASK_SEND = 1;            // need to send IKCP_CMD_WASK
    static const uint32_t IKCP_ASK_TELL = 2;            // need to send IKCP_CMD_WINS

    static const uint32_t IKCP_RTO_NDL = 30;            // no delay min rto
    static const uint32_t IKCP_RTO_MIN = 100;           // normal min rto
    static const uint32_t IKCP_RTO_DEF = 200;
    static const uint32_t IKCP_RTO_MAX = 60000;

    static const uint32_t IKCP_WND_SND = 32;
    static const uint32_t IKCP_WND_RCV = 128;           // must >= max fragment size
    static const uint32_t IKCP_MTU_DEF = 1400;
    static const uint32_t IKCP_ACK_FAST = 3;
    static const uint32_t IKCP_INTERVAL = 100;
    static const uint32_t IKCP_THRESH_INIT = 2;
    static const uint32_t IKCP_THRESH_MIN = 2;
    static const uint32_t IKCP_PROBE_INIT = 7000;       // 7 secs to probe window size
    static const uint32_t IKCP_PROBE_LIMIT = 120000;    // up to 120 secs to probe window

    using onReadCB = std::function<void(const Buffer::Ptr &buf)>;
    using onWriteCB = std::function<void(const Buffer::Ptr &buf)>;
    using OnErr = std::function<void(const SockException &)>;

    KcpTransport(bool serverMode);
    KcpTransport(bool serverMode, const EventPoller::Ptr &poller);
    virtual ~KcpTransport();

    void setOnRead(onReadCB cb) { _on_read = std::move(cb); }
    void setOnWrite(onWriteCB cb) { _on_write = std::move(cb); }
    void setOnErr(OnErr cb) { _on_err = std::move(cb); }

    void setPoller(const EventPoller::Ptr &poller) {
        _poller = poller ? poller : EventPollerPool::Instance().getPoller();
    }

    // The application layer puts data into the send queue
    ssize_t send(const Buffer::Ptr &buf, bool flush = false);

    // The application layer inputs the data received by the socket layer
    void input(const Buffer::Ptr &buf);

    // change MTU size, default is 1400
    void setMtu(int mtu);

    void setInterval(int intervoal);

    void setRxMinrto(int rx_minrto);

    // set maximum window size: sndwnd=32, rcvwnd=32 by default
    void setWndSize(int sndwnd, int rcvwnd);

    // Set low delay mode
    // Default is DELAY_MODE_NORMAL
    void setDelayMode(DelayMode delay_mode);

    // Set fast retransmission threshold
    // Default is 0, meaning no fast retransmission
    void setFastResend(int resend);

    // Set fast retransmission conservative mode
    // Default is conservative mode
    void setFastackConserve(bool flag);

    // Set whether to disable congestion control
    // Default is enabled
    void setNoCwnd(bool flag);

    // Set whether to enable stream mode
    // Default is disabled
    void setStreamMode(bool flag);

protected:

    void onWrite(const Buffer::Ptr &buf) {
        if (_on_write) {
            _on_write(buf);
        }
    }

    void onRead(const Buffer::Ptr &buf) {
        if (_on_read) {
            _on_read(buf);
        }
    }

    void onErr(const SockException &err) {
        DebugL;
        if (_on_err) {
            _on_err(err);
        }
    }

    void startTimer();

    // Process received data, called when there is new data in rcv_buf
    void onData();

    // Measure the length of the next packet that can be extracted from rcv_queue
    int peeksize();

    void handleAnyPacket(KcpPacket::Ptr packet);
    void handleCmdAck(KcpPacket::Ptr packet, uint32_t current);
    void handleCmdPush(KcpPacket::Ptr packet);

    // move available data from rcv_buf -> rcv_queue
    void sortRecvBuf();
    void sortSendQueue();
    // In stream mode, merge send packets
    size_t mergeSendQueue(const char *buffer, size_t len);

    // Actually send the data in the send queue
    void update();
    void sendSendQueue();
    void sendAckList();
    void sendProbePacket();
    void sendPacket(KcpPacket::Ptr pkt, bool flush = false);
    void flushPool();

    // Discard packets in the send cache that have been acknowledged by the peer
    // In UNA mode, all packets before the specified sequence number have been acknowledged and can be dropped
    void dropCacheByUna(uint32_t una);

    // Discard packets in the send cache that have been acknowledged by the peer
    // In ACK mode, only the packet with the specified sequence number is acknowledged
    void dropCacheByAck(uint32_t sn);

    // Update RTT
    void updateRtt(int32_t rtt);

    // Update the Fastack count of packets in the send cache
    void updateFastAck(uint32_t sn, uint32_t ts);

    // Increase congestion window
    void increaseCwnd();

    // Decrease congestion window
    void decreaseCwnd(bool change, bool lost);

    // Get how many packets are waiting to be sent
    int getWaitSnd();

    int getRcvWndUnused();

private:
    onReadCB _on_read = nullptr;
    onWriteCB _on_write = nullptr;
    OnErr _on_err = nullptr;

    bool _server_mode;
    bool _conv_init = false;

    EventPoller::Ptr _poller = nullptr;
    Timer::Ptr _timer;
    // Refresh timer
    Ticker _alive_ticker;

    bool _fastack_conserve = false;  // Fast retransmission conservative mode

    uint32_t _conv;    // Conversation ID, used to identify a session
    uint32_t _mtu  = IKCP_MTU_DEF;     // Maximum Transmission Unit, default 1400
    uint32_t _mss  = IKCP_MTU_DEF - KcpPacket::HEADER_SIZE;     // Maximum segment size, calculated from MTU

    uint32_t _interval = IKCP_INTERVAL;  // Internal flush interval

    uint32_t _fastresend = 0;  // Fast retransmission trigger threshold, when a packet's _fastack exceeds this value, fast retransmission is triggered
    int _fastlimit = 5;   // Fast retransmission limit, limits the maximum number of times fast retransmission is triggered to prevent excessive retransmission

    uint32_t _xmit = 0;      // Retransmission count
    uint32_t _dead_link = 20; // Maximum retransmission count, when a packet's retransmission count exceeds this value, the link is considered broken
    uint32_t _snd_una = 0; // First unacknowledged packet sequence number in the send buffer
    uint32_t _snd_nxt = 0; // Next sequence number to be assigned
    uint32_t _rcv_nxt = 0; // Next packet sequence number to be received in the receive queue

    uint32_t _ts_recent = 0; // Timestamp of the most recent received packet
    uint32_t _ts_lastack = 0;// Timestamp of the most recent sent ACK

    // rtt
    int32_t _rx_rttval = 0;  //RTT variance
    int32_t _rx_srtt = 0;    //RTT (after smoothing)
    int32_t _rx_rto = IKCP_RTO_DEF; // Retransmission timeout (dynamically adjusted based on RTT and RTT variance)
    int32_t _rx_minrto = IKCP_RTO_MIN; // Minimum retransmission timeout to prevent RTO from being too small

    // For congestion window control
    uint32_t _snd_wnd = IKCP_WND_SND; // Send queue window, used to limit send rate, user-configured (in segments)
    uint32_t _rcv_wnd = IKCP_WND_RCV; // Receive queue window, used to limit receive rate, user-configured (in segments)
    uint32_t _rmt_wnd = IKCP_WND_RCV; // Remote receive buffer congestion window, announced by the remote (in segments)
    uint32_t _cwnd = 1;  // Send buffer congestion window size, dynamically adjusted by algorithm (in segments)
    uint32_t _incr = 0;  // Congestion window increment, used for dynamic window size in congestion control algorithm (in bytes)
    uint32_t _ssthresh = IKCP_THRESH_INIT;  // Slow start threshold
    uint32_t _probe = 0;   // Probe flag, used to probe the remote window size
    uint32_t _ts_probe = 0; // Probe timestamp, records the time when the probe packet was sent
    uint32_t _probe_wait = 0;// Probe wait time, controls the interval between probe packet transmissions

    DelayMode _delay_mode = DELAY_MODE_NORMAL;
    int _nocwnd = false; // Whether to disable congestion control
    bool _stream = false; // Whether to enable stream transmission mode

    // Transmission link: userdata->_snd_queue->_snd_buf->network send
    //_snd_queue: unlimited
    //_snd_buf: min(_snd_wnd, _rmt_wnd, _cwnd)
    // Transmission link: network receive->_rcv_buf->_snd_queue->userdata
    //_rcv_buf: unlimited, stores out-of-order data temporarily
    //_snd_queue: _rcv_wnd
    std::list<KcpDataPacket::Ptr> _snd_queue; // Send queue, packets not yet in the send window
    std::list<KcpDataPacket::Ptr> _rcv_queue; // Receive queue, fully received packets waiting to be delivered to the application layer
    std::list<KcpDataPacket::Ptr> _snd_buf;   // Send buffer, packets already in the send window, used for retransmission
    std::list<KcpDataPacket::Ptr> _rcv_buf;   // Receive buffer, packets already received but not yet deliverable to the application layer due to out-of-order or packet loss
    // List of ACKs to be sent
    std::deque<std::pair<uint32_t /*sn*/, uint32_t /*ts*/>>_acklist;
    BufferRaw::Ptr _buffer_pool;  // Used to merge multiple KCP packets into one UDP packet
};
} // namespace toolkit

#endif // TOOLKIT_NETWORK_KCP_H
