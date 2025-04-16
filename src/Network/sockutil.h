#ifndef NETWORK_SOCKUTIL_H
#define NETWORK_SOCKUTIL_H

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment (lib, "Ws2_32.lib")
#pragma comment(lib,"Iphlpapi.lib")
#else
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif // defined(_WIN32)

#include <cstring>
#include <cstdint>
#include <map>
#include <vector>
#include <string>

namespace toolkit {

#if defined(_WIN32)
#ifndef socklen_t
#define socklen_t int
#endif //!socklen_t
int ioctl(int fd, long cmd, u_long *ptr);
int close(int fd);
#endif // defined(_WIN32)

#if !defined(SOCKET_DEFAULT_BUF_SIZE)
#define SOCKET_DEFAULT_BUF_SIZE (256 * 1024)
#else
#if SOCKET_DEFAULT_BUF_SIZE == 0 && !defined(__linux__)
// just for linux, because in some high-throughput environments,
// kernel control is more efficient and reasonable than program
// settings. For example, refer to cloudflare's blog
#undef SOCKET_DEFAULT_BUF_SIZE
#define SOCKET_DEFAULT_BUF_SIZE (256 * 1024)
#endif
#endif
#define TCP_KEEPALIVE_INTERVAL 30
#define TCP_KEEPALIVE_PROBE_TIMES 9
#define TCP_KEEPALIVE_TIME 120

//Socket tool class, encapsulating some basic socket and network operations
class SockUtil {
public:
    /**
     * Create a TCP client socket and connect to the server
     * @param host Server IP or domain name
     * @param port Server port number
     * @param async Whether to connect asynchronously
     * @param local_ip Local network card IP to bind
     * @param local_port Local port number to bind
     * @return -1 represents failure, others are socket fd numbers
     */
    static int connect(const char *host, uint16_t port, bool async = true, const char *local_ip = "::", uint16_t local_port = 0);

    /**
     * Create a TCP listening socket
     * @param port Local port to listen on
     * @param local_ip Local network card IP to bind
     * @param back_log Accept queue length
     * @return -1 represents failure, others are socket fd numbers
     */
    static int listen(const uint16_t port, const char *local_ip = "::", int back_log = 1024);

    /**
     * Create a UDP socket
     * @param port Local port to listen on
     * @param local_ip Local network card IP to bind
     * @param enable_reuse Whether to allow repeated bind port
     * @return -1 represents failure, others are socket fd numbers
     */
    static int bindUdpSock(const uint16_t port, const char *local_ip = "::", bool enable_reuse = true);

    /**
     * @brief Release the binding relationship related to sock
     * @param sock, socket fd number
     * @return 0 Success, -1 Failure
     */
    static int dissolveUdpSock(int sock);

    /**
     * Enable TCP_NODELAY to reduce TCP interaction delay
     * @param fd socket fd number
     * @param on Whether to enable
     * @return 0 represents success, -1 represents failure
     */
    static int setNoDelay(int fd, bool on = true);

    /**
     * Write socket does not trigger SIG_PIPE signal (seems to be effective only on Mac)
     * @param fd socket fd number
     * @return 0 represents success, -1 represents failure
     */
    static int setNoSigpipe(int fd);

    /**
     * Set whether the read and write socket is blocked
     * @param fd socket fd number
     * @param noblock Whether to block
     * @return 0 represents success, -1 represents failure
     */
    static int setNoBlocked(int fd, bool noblock = true);

    /**
     * Set the socket receive buffer, default is around 8K, generally has an upper limit
     * Can be adjusted through kernel configuration file
     * @param fd socket fd number
     * @param size Receive buffer size
     * @return 0 represents success, -1 represents failure
     */
    static int setRecvBuf(int fd, int size = SOCKET_DEFAULT_BUF_SIZE);

    /**
     * Set the socket receive buffer, default is around 8K, generally has an upper limit
     * Can be adjusted through kernel configuration file
     * @param fd socket fd number
     * @param size Receive buffer size
     * @return 0 represents success, -1 represents failure
     */
    static int setSendBuf(int fd, int size = SOCKET_DEFAULT_BUF_SIZE);

    /**
     * Set subsequent bindable reuse port (in TIME_WAIT state)
     * @param fd socket fd number
     * @param on whether to enable this feature
     * @return 0 represents success, -1 for failure
     */
    static int setReuseable(int fd, bool on = true, bool reuse_port = true);

    /**
     * Run sending or receiving UDP broadcast messages
     * @param fd socket fd number
     * @param on whether to enable this feature
     * @return 0 represents success, -1 for failure
     */
    static int setBroadcast(int fd, bool on = true);

    /**
     * Enable TCP KeepAlive feature
     * @param fd socket fd number
     * @param on whether to enable this feature
     * @param idle keepalive idle time
     * @param interval keepalive probe time interval
     * @param times keepalive probe times
     * @return 0 represents success, -1 for failure
     */
    static int setKeepAlive(int fd, bool on = true, int interval = TCP_KEEPALIVE_INTERVAL, int idle = TCP_KEEPALIVE_TIME, int times = TCP_KEEPALIVE_PROBE_TIMES);

    /**
     * Enable FD_CLOEXEC feature (related to multiple processes)
     * @param fd fd number, not necessarily a socket
     * @param on whether to enable this feature
     * @return 0 represents success, -1 for failure
     */
    static int setCloExec(int fd, bool on = true);

    /**
     * Enable SO_LINGER feature
     * @param sock socket fd number
     * @param second kernel waiting time for closing socket timeout, in seconds
     * @return 0 represents success, -1 for failure
     */
    static int setCloseWait(int sock, int second = 0);

    /**
     * DNS resolution
     * @param host domain name or IP
     * @param port port number
     * @param addr sockaddr structure
     * @return whether successful
     */
    static bool getDomainIP(const char *host, uint16_t port, struct sockaddr_storage &addr, int ai_family = AF_INET,
                            int ai_socktype = SOCK_STREAM, int ai_protocol = IPPROTO_TCP, int expire_sec = 60);

    /**
     * Set multicast TTL
     * @param sock socket fd number
     * @param ttl TTL value
     * @return 0 represents success, -1 for failure
     */
    static int setMultiTTL(int sock, uint8_t ttl = 64);

    /**
     * Set multicast sending network card
     * @param sock socket fd number
     * @param local_ip local network card IP
     * @return 0 represents success, -1 for failure
     */
    static int setMultiIF(int sock, const char *local_ip);

    /**
     * Set whether to receive multicast packets sent by the local machine
     * @param fd socket fd number
     * @param acc whether to receive
     * @return 0 represents success, -1 for failure
     */
    static int setMultiLOOP(int fd, bool acc = false);

    /**
     * Join multicast
     * @param fd socket fd number
     * @param addr multicast address
     * @param local_ip local network card IP
     * @return 0 represents success, -1 for failure
     */
    static int joinMultiAddr(int fd, const char *addr, const char *local_ip = "0.0.0.0");

    /**
     * Exit multicast
     * @param fd socket fd number
     * @param addr multicast address
     * @param local_ip local network card ip
     * @return 0 represents success, -1 for failure
     */
    static int leaveMultiAddr(int fd, const char *addr, const char *local_ip = "0.0.0.0");

    /**
     * Join multicast and only receive multicast data from the specified source
     * @param sock socket fd number
     * @param addr multicast address
     * @param src_ip source address
     * @param local_ip local network card ip
     * @return 0 represents success, -1 for failure
     */
    static int joinMultiAddrFilter(int sock, const char *addr, const char *src_ip, const char *local_ip = "0.0.0.0");

    /**
     * Exit multicast
     * @param fd socket fd number
     * @param addr multicast address
     * @param src_ip source address
     * @param local_ip local network card ip
     * @return 0 represents success, -1 for failure
     */
    static int leaveMultiAddrFilter(int fd, const char *addr, const char *src_ip, const char *local_ip = "0.0.0.0");

    /**
     * Get the current error of the socket
     * @param fd socket fd number
     * @return error code
     */
    static int getSockError(int fd);

    /**
     * Get the list of network cards
     * @return vector<map<ip:name> >
     */
    static std::vector<std::map<std::string, std::string>> getInterfaceList();

    /**
     * Get the default local ip of the host
     */
    static std::string get_local_ip();

    /**
     * Get the local ip bound to the socket
     * @param sock socket fd number
     */
    static std::string get_local_ip(int sock);

    /**
     * Get the local port bound to the socket
     * @param sock socket fd number
     */
    static uint16_t get_local_port(int sock);

    /**
     * Get the remote ip bound to the socket
     * @param sock socket fd number
     */
    static std::string get_peer_ip(int sock);

    /**
     * Get the remote port bound to the socket
     * @param sock socket fd number
     */
    static uint16_t get_peer_port(int sock);

    static bool support_ipv6();
    /**
     * Thread-safe conversion of in_addr to IP string
     */
    static std::string inet_ntoa(const struct in_addr &addr);
    static std::string inet_ntoa(const struct in6_addr &addr);
    static std::string inet_ntoa(const struct sockaddr *addr);
    static uint16_t inet_port(const struct sockaddr *addr);
    static struct sockaddr_storage make_sockaddr(const char *ip, uint16_t port);
    static socklen_t get_sock_len(const struct sockaddr *addr);
    static bool get_sock_local_addr(int fd, struct sockaddr_storage &addr);
    static bool get_sock_peer_addr(int fd, struct sockaddr_storage &addr);

    /**
     * Get the IP of the network card
     * @param if_name Network card name
     */
    static std::string get_ifr_ip(const char *if_name);

    /**
     * Get the network card name
     * @param local_op Network card IP
     */
    static std::string get_ifr_name(const char *local_op);

    /**
     * Get the subnet mask based on the network card name
     * @param if_name Network card name
     */
    static std::string get_ifr_mask(const char *if_name);

    /**
     * Get the broadcast address based on the network card name
     * @param if_name Network card name
     */
    static std::string get_ifr_brdaddr(const char *if_name);

    /**
     * Determine if two IPs are in the same network segment
     * @param src_ip My IP
     * @param dts_ip Peer IP
     */
    static bool in_same_lan(const char *src_ip, const char *dts_ip);

    /**
     * Determine if it is an IPv4 address
     */
    static bool is_ipv4(const char *str);

    /**
     * Determine if it is an IPv6 address
     */
    static bool is_ipv6(const char *str);
};

}  // namespace toolkit
#endif // !NETWORK_SOCKUTIL_H
