#ifndef CRYPTO_SSLBOX_H_
#define CRYPTO_SSLBOX_H_

#include <mutex>
#include <string>
#include <functional>
#include "logger.h"
#include "List.h"
#include "util.h"
#include "Network/Buffer.h"
#include "ResourcePool.h"

typedef struct x509_st X509;
typedef struct evp_pkey_st EVP_PKEY;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;
typedef struct bio_st BIO;

namespace toolkit {

class SSL_Initor {
public:
    friend class SSL_Box;

    static SSL_Initor &Instance();

    /**
     * Load public and private keys from a file or string
     * The certificate file must contain both public and private keys (cer format certificates only include public keys, use the following method to load)
     * The client can load the certificate by default (unless the server requires the client to provide a certificate)
     * @param pem_or_p12 pem or p12 file path or file content string
     * @param server_mode Whether it is in server mode
     * @param password Private key encryption password
     * @param is_file Whether the parameter pem_or_p12 is a file path
     * @param is_default Whether it is the default certificate
     */
    bool loadCertificate(const std::string &pem_or_p12, bool server_mode = true, const std::string &password = "",
                         bool is_file = true, bool is_default = true);

    /**
     * Whether to ignore invalid certificates
     * Ignore by default, strongly not recommended!
     * @param ignore Flag
     */
    void ignoreInvalidCertificate(bool ignore = true);

    /**
     * Trust a certain certificate, generally used for clients to trust self-signed certificates or certificates signed by self-signed CAs
     * For example, if my client wants to trust a certificate I issued myself, we can only trust this certificate
     * @param pem_p12_cer pem file or p12 file or cer file path or content
     * @param server_mode Whether it is in server mode
     * @param password pem or p12 certificate password
     * @param is_file Whether it is a file path
     * @return Whether the loading is successful
     */
    bool trustCertificate(const std::string &pem_p12_cer, bool server_mode = false, const std::string &password = "",
                          bool is_file = true);

    /**
     * Trust a certain certificate
     * @param cer Certificate public key
     * @param server_mode Whether it is in server mode
     * @return Whether the loading is successful
     */
    bool trustCertificate(X509 *cer, bool server_mode = false);

    /**
     * Get the SSL_CTX object based on the virtual host
     * @param vhost Virtual host name
     * @param server_mode Whether it is in server mode
     * @return SSL_CTX object
     */
    std::shared_ptr<SSL_CTX> getSSLCtx(const std::string &vhost, bool server_mode);

private:
    SSL_Initor();
    ~SSL_Initor();

    /**
     * Create an SSL object
     */
    std::shared_ptr<SSL> makeSSL(bool server_mode);

    /**
     * Set the ssl context
     * @param vhost Virtual host name
     * @param ctx ssl context
     * @param server_mode ssl context
     * @param is_default Whether it is the default certificate
     */
    bool setContext(const std::string &vhost, const std::shared_ptr<SSL_CTX> &ctx, bool server_mode, bool is_default = true);

    /**
     * Set the default configuration for SSL_CTX
     * @param ctx Object pointer
     */
    static void setupCtx(SSL_CTX *ctx);

    std::shared_ptr<SSL_CTX> getSSLCtx_l(const std::string &vhost, bool server_mode);

    std::shared_ptr<SSL_CTX> getSSLCtxWildcards(const std::string &vhost, bool server_mode);

    /**
     * Get the default virtual host
     */
    std::string defaultVhost(bool server_mode);

    /**
     * Callback function for completing vhost name matching
     */
    static int findCertificate(SSL *ssl, int *ad, void *arg);

private:
    struct less_nocase {
        bool operator()(const std::string &x, const std::string &y) const {
            return strcasecmp(x.data(), y.data()) < 0;
        }
    };

private:
    std::recursive_mutex _mtx;
    std::string _default_vhost[2];
    std::shared_ptr<SSL_CTX> _ctx_empty[2];
    std::map<std::string, std::shared_ptr<SSL_CTX>, less_nocase> _ctxs[2];
    std::map<std::string, std::shared_ptr<SSL_CTX>, less_nocase> _ctxs_wildcards[2];
};

////////////////////////////////////////////////////////////////////////////////////

class SSL_Box {
public:
    SSL_Box(bool server_mode = true, bool enable = true, int buff_size = 32 * 1024);

    ~SSL_Box();

    /**
     * Decrypts the received ciphertext after calling this function
     * @param buffer Received ciphertext data
     */
    void onRecv(const Buffer::Ptr &buffer);

    /**
     * Calls this function to encrypt the plaintext that needs to be encrypted
     * @param buffer Plaintext data that needs to be encrypted
     */
    void onSend(Buffer::Ptr buffer);

    /**
     * Sets the callback to get the plaintext after decryption
     * @param cb Callback object
     */
    void setOnDecData(const std::function<void(const Buffer::Ptr &)> &cb);

    /**
     * Sets the callback to get the ciphertext after encryption
     * @param cb Callback object
     */
    void setOnEncData(const std::function<void(const Buffer::Ptr &)> &cb);

    /**
     * Terminates SSL
     */
    void shutdown();

    /**
     * Clears data
     */
    void flush();

    /**
     * Sets the virtual host name
     * @param host Virtual host name
     * @return Whether the operation was successful
     */
    bool setHost(const char *host);

private:
    void flushWriteBio();

    void flushReadBio();

private:
    bool _server_mode;
    bool _send_handshake;
    bool _is_flush = false;
    int _buff_size;
    BIO *_read_bio;
    BIO *_write_bio;
    std::shared_ptr<SSL> _ssl;
    List <Buffer::Ptr> _buffer_send;
    ResourcePool <BufferRaw> _buffer_pool;
    std::function<void(const Buffer::Ptr &)> _on_dec;
    std::function<void(const Buffer::Ptr &)> _on_enc;
};

} /* namespace toolkit */
#endif /* CRYPTO_SSLBOX_H_ */
