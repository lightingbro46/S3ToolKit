#ifndef S3TOOLKIT_SSLUTIL_H
#define S3TOOLKIT_SSLUTIL_H

#include <memory>
#include <string>
#include <vector>

typedef struct x509_st X509;
typedef struct evp_pkey_st EVP_PKEY;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;
typedef struct bio_st BIO;

namespace toolkit {
/**
 * SSL certificate suffixes are generally divided into the following types
 * pem: This is a base64 character encoded string, which may contain a public key, private key, or both
 * cer: Only and must be a public key, can be used with pem private key
 * p12: Must include both private key and public key
 */
class SSLUtil {
public:
    static std::string getLastError();

    /**
     * Load public key certificate, support pem, p12, cer suffixes
     * When openssl loads p12 certificate, it will verify whether the public key and private key match,
     * so when loading p12 public key, you may need to pass in the certificate password
     * @param file_path_or_data File path or file content
     * @param isFile Whether it is a file
     * @return Public key certificate list
     */
    static std::vector<std::shared_ptr<X509> > loadPublicKey(const std::string &file_path_or_data, const std::string &passwd = "", bool isFile = true);

    /**
     * Load private key certificate, support pem, p12 suffixes
     * @param file_path_or_data File path or file content
     * @param passwd Password
     * @param isFile Whether it is a file
     * @return Private key certificate
     */
    static std::shared_ptr<EVP_PKEY> loadPrivateKey(const std::string &file_path_or_data, const std::string &passwd = "", bool isFile = true);

    /**
     * Create SSL_CTX object
     * @param cer Public key array
     * @param key Private key
     * @param serverMode Whether it is server mode or client mode
     * @return SSL_CTX object
     */
    static std::shared_ptr<SSL_CTX> makeSSLContext(const std::vector<std::shared_ptr<X509> > &cers, const std::shared_ptr<EVP_PKEY> &key, bool serverMode = true, bool checkKey = false);

    /**
     * Create ssl object
     * @param ctx SSL_CTX object
     */
    static std::shared_ptr<SSL> makeSSL(SSL_CTX *ctx);

    /**
     * specifies that the default locations from which CA certificates are loaded should be used.
     * There is one default directory and one default file.
     * The default CA certificates directory is called "certs" in the default OpenSSL directory.
     * Alternatively the SSL_CERT_DIR environment variable can be defined to override this location.
     * The default CA certificates file is called "cert.pem" in the default OpenSSL directory.
     *  Alternatively the SSL_CERT_FILE environment variable can be defined to override this location.
     * Trust all certificates in the /usr/local/ssl/certs/ directory and /usr/local/ssl/cert.pem
     * The environment variable SSL_CERT_FILE will replace the path of /usr/local/ssl/cert.pem
     */
    static bool loadDefaultCAs(SSL_CTX *ctx);

    /**
     * Trust a public key
     */
    static bool trustCertificate(SSL_CTX *ctx, X509 *cer);


    /**
     * Verify the validity of the certificate
     * @param cer Certificate to be verified
     * @param ... Trusted CA root certificates, X509 type, ending with nullptr
     * @return Whether it is valid
     */
    static bool verifyX509(X509 *cer, ...);

    /**
     * Use public key to encrypt and decrypt data
     * @param cer Public key, must be ras public key
     * @param in_str Original data to be encrypted or decrypted, tested to support up to 245 bytes,
     *                encrypted data length is fixed at 256 bytes
     * @param enc_or_dec true: Encrypt, false: Decrypt
     * @return Encrypted or decrypted data
     */
    static std::string cryptWithRsaPublicKey(X509 *cer, const std::string &in_str, bool enc_or_dec);

    /**
     * Use private key to encrypt and decrypt data
     * @param private_key Private key, must be ras private key
     * @param in_str Original data to be encrypted or decrypted, tested to support up to 245 bytes,
     *                encrypted data length is fixed at 256 bytes
     * @param enc_or_dec true: Encrypt, false: Decrypt
     * @return Encrypted or decrypted data
     */
    static std::string cryptWithRsaPrivateKey(EVP_PKEY *private_key, const std::string &in_str, bool enc_or_dec);

    /**
     * Get certificate domain name
     * @param cer Certificate public key
     * @return Certificate domain name
     */
    static std::string getServerName(X509 *cer);
};

}//namespace toolkit
#endif //S3TOOLKIT_SSLUTIL_H
