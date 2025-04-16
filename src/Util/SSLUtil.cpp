#include "SSLUtil.h"
#include "onceToken.h"
#include "logger.h"

#if defined(ENABLE_OPENSSL)
#include <openssl/bio.h>
#include <openssl/ossl_typ.h>
#include <openssl/pkcs12.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/conf.h>
#endif // defined(ENABLE_OPENSSL)

using namespace std;

namespace toolkit
{

    std::string SSLUtil::getLastError()
    {
#if defined(ENABLE_OPENSSL)
        unsigned long errCode = ERR_get_error();
        if (errCode != 0)
        {
            char buffer[256];
            ERR_error_string_n(errCode, buffer, sizeof(buffer));
            return buffer;
        }
        else
#endif // defined(ENABLE_OPENSSL)
        {
            return "No error";
        }
    }

#if defined(ENABLE_OPENSSL)

    static int getCerType(BIO *bio, const char *passwd, X509 **x509, int type)
    {
        // Try pem format
        if (type == 1 || type == 0)
        {
            if (type == 0)
            {
                BIO_reset(bio);
            }
            // Try PEM format
            *x509 = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
            if (*x509)
            {
                return 1;
            }
        }

        if (type == 2 || type == 0)
        {
            if (type == 0)
            {
                BIO_reset(bio);
            }
            // Try DER format
            *x509 = d2i_X509_bio(bio, nullptr);
            if (*x509)
            {
                return 2;
            }
        }

        if (type == 3 || type == 0)
        {
            if (type == 0)
            {
                BIO_reset(bio);
            }
            // Try p12 format
            PKCS12 *p12 = d2i_PKCS12_bio(bio, nullptr);
            if (p12)
            {
                EVP_PKEY *pkey = nullptr;
                PKCS12_parse(p12, passwd, &pkey, x509, nullptr);
                PKCS12_free(p12);
                if (pkey)
                {
                    EVP_PKEY_free(pkey);
                }
                if (*x509)
                {
                    return 3;
                }
            }
        }

        return 0;
    }

#endif // defined(ENABLE_OPENSSL)

    vector<shared_ptr<X509>> SSLUtil::loadPublicKey(const string &file_path_or_data, const string &passwd, bool isFile)
    {
        vector<shared_ptr<X509>> ret;
#if defined(ENABLE_OPENSSL)
        BIO *bio = isFile ? BIO_new_file((char *)file_path_or_data.data(), "r") : BIO_new_mem_buf((char *)file_path_or_data.data(), file_path_or_data.size());
        if (!bio)
        {
            WarnL << (isFile ? "BIO_new_file" : "BIO_new_mem_buf") << " failed: " << getLastError();
            return ret;
        }

        onceToken token0(nullptr, [&]()
                         { BIO_free(bio); });

        int cer_type = 0;
        X509 *x509 = nullptr;
        do
        {
            cer_type = getCerType(bio, passwd.data(), &x509, cer_type);
            if (cer_type)
            {
                ret.push_back(shared_ptr<X509>(x509, [](X509 *ptr)
                                               { X509_free(ptr); }));
            }
        } while (cer_type != 0);
        return ret;
#else
        return ret;
#endif // defined(ENABLE_OPENSSL)
    }

    shared_ptr<EVP_PKEY> SSLUtil::loadPrivateKey(const string &file_path_or_data, const string &passwd, bool isFile)
    {
#if defined(ENABLE_OPENSSL)
        BIO *bio = isFile ? BIO_new_file((char *)file_path_or_data.data(), "r") : BIO_new_mem_buf((char *)file_path_or_data.data(), file_path_or_data.size());
        if (!bio)
        {
            WarnL << (isFile ? "BIO_new_file" : "BIO_new_mem_buf") << " failed: " << getLastError();
            return nullptr;
        }

        pem_password_cb *cb = [](char *buf, int size, int rwflag, void *userdata) -> int
        {
            const string *passwd = (const string *)userdata;
            size = size < (int)passwd->size() ? size : (int)passwd->size();
            memcpy(buf, passwd->data(), size);
            return size;
        };

        onceToken token0(nullptr, [&]()
                         { BIO_free(bio); });

        // Try pem format
        EVP_PKEY *evp_key = PEM_read_bio_PrivateKey(bio, nullptr, cb, (void *)&passwd);
        if (!evp_key)
        {
            // Try p12 format
            BIO_reset(bio);
            PKCS12 *p12 = d2i_PKCS12_bio(bio, nullptr);
            if (!p12)
            {
                return nullptr;
            }
            X509 *x509 = nullptr;
            PKCS12_parse(p12, passwd.data(), &evp_key, &x509, nullptr);
            PKCS12_free(p12);
            if (x509)
            {
                X509_free(x509);
            }
            if (!evp_key)
            {
                return nullptr;
            }
        }

        return shared_ptr<EVP_PKEY>(evp_key, [](EVP_PKEY *ptr)
                                    { EVP_PKEY_free(ptr); });
#else
        return nullptr;
#endif // defined(ENABLE_OPENSSL)
    }

    shared_ptr<SSL_CTX> SSLUtil::makeSSLContext(const vector<shared_ptr<X509>> &cers, const shared_ptr<EVP_PKEY> &key, bool serverMode, bool checkKey)
    {
#if defined(ENABLE_OPENSSL)
        SSL_CTX *ctx = SSL_CTX_new(serverMode ? SSLv23_server_method() : SSLv23_client_method());
        if (!ctx)
        {
            WarnL << "SSL_CTX_new " << (serverMode ? "SSLv23_server_method" : "SSLv23_client_method") << " failed: " << getLastError();
            return nullptr;
        }
        int i = 0;
        for (auto &cer : cers)
        {
            // Load public key
            if (i++ == 0)
            {
                // SSL_CTX_use_certificate internally calls X509_up_ref, so no need to use X509_dup here
                SSL_CTX_use_certificate(ctx, cer.get());
            }
            else
            {
                // Need to copy X509 object first, otherwise the pointer will be invalid
                SSL_CTX_add_extra_chain_cert(ctx, X509_dup(cer.get()));
            }
        }

        if (key)
        {
            // Provided private key
            if (SSL_CTX_use_PrivateKey(ctx, key.get()) != 1)
            {
                WarnL << "SSL_CTX_use_PrivateKey failed: " << getLastError();
                SSL_CTX_free(ctx);
                return nullptr;
            }
        }

        if (key || checkKey)
        {
            // Private key loaded successfully
            if (SSL_CTX_check_private_key(ctx) != 1)
            {
                WarnL << "SSL_CTX_check_private_key failed: " << getLastError();
                SSL_CTX_free(ctx);
                return nullptr;
            }
        }

        // Public and private key match or no public and private key
        return shared_ptr<SSL_CTX>(ctx, [](SSL_CTX *ptr)
                                   { SSL_CTX_free(ptr); });
#else
        return nullptr;
#endif // defined(ENABLE_OPENSSL)
    }

    shared_ptr<SSL> SSLUtil::makeSSL(SSL_CTX *ctx)
    {
#if defined(ENABLE_OPENSSL)
        auto *ssl = SSL_new(ctx);
        if (!ssl)
        {
            return nullptr;
        }
        return shared_ptr<SSL>(ssl, [](SSL *ptr)
                               { SSL_free(ptr); });
#else
        return nullptr;
#endif // defined(ENABLE_OPENSSL)
    }

    bool SSLUtil::loadDefaultCAs(SSL_CTX *ctx)
    {
#if defined(ENABLE_OPENSSL)
        if (!ctx)
        {
            return false;
        }

        if (SSL_CTX_set_default_verify_paths(ctx) != 1)
        {
            WarnL << "SSL_CTX_set_default_verify_paths failed: " << getLastError();
            return false;
        }
        return true;
#else
        return false;
#endif // defined(ENABLE_OPENSSL)
    }

    bool SSLUtil::trustCertificate(SSL_CTX *ctx, X509 *cer)
    {
#if defined(ENABLE_OPENSSL)
        X509_STORE *store = SSL_CTX_get_cert_store(ctx);
        if (store && cer)
        {
            if (X509_STORE_add_cert(store, cer) != 1)
            {
                WarnL << "X509_STORE_add_cert failed: " << getLastError();
                return false;
            }
            return true;
        }
#endif // defined(ENABLE_OPENSSL)
        return false;
    }

    bool SSLUtil::verifyX509(X509 *cer, ...)
    {
#if defined(ENABLE_OPENSSL)
        va_list args;
        va_start(args, cer);
        X509_STORE *store = X509_STORE_new();
        do
        {
            X509 *ca;
            if ((ca = va_arg(args, X509 *)) == nullptr)
            {
                break;
            }
            X509_STORE_add_cert(store, ca);
        } while (true);
        va_end(args);

        X509_STORE_CTX *store_ctx = X509_STORE_CTX_new();
        X509_STORE_CTX_init(store_ctx, store, cer, nullptr);
        auto ret = X509_verify_cert(store_ctx);
        if (ret != 1)
        {
            int depth = X509_STORE_CTX_get_error_depth(store_ctx);
            int err = X509_STORE_CTX_get_error(store_ctx);
            WarnL << "X509_verify_cert failed, depth: " << depth << ", err: " << X509_verify_cert_error_string(err);
        }

        X509_STORE_CTX_free(store_ctx);
        X509_STORE_free(store);
        return ret == 1;
#else
        WarnL << "ENABLE_OPENSSL disabled, you can not use any features based on openssl";
        return false;
#endif // defined(ENABLE_OPENSSL)
    }

#ifdef ENABLE_OPENSSL
#ifndef X509_F_X509_PUBKEY_GET0
    EVP_PKEY *X509_get0_pubkey(X509 *x)
    {
        EVP_PKEY *ret = X509_get_pubkey(x);
        if (ret)
        {
            EVP_PKEY_free(ret);
        }
        return ret;
    }
#endif // X509_F_X509_PUBKEY_GET0

#ifndef EVP_F_EVP_PKEY_GET0_RSA
    RSA *EVP_PKEY_get0_RSA(EVP_PKEY *pkey)
    {
        RSA *ret = EVP_PKEY_get1_RSA(pkey);
        if (ret)
        {
            RSA_free(ret);
        }
        return ret;
    }
#endif // EVP_F_EVP_PKEY_GET0_RSA
#endif // ENABLE_OPENSSL

    string SSLUtil::cryptWithRsaPublicKey(X509 *cer, const string &in_str, bool enc_or_dec)
    {
#if defined(ENABLE_OPENSSL)
        EVP_PKEY *public_key = X509_get0_pubkey(cer);
        if (!public_key)
        {
            return "";
        }
        auto rsa = EVP_PKEY_get1_RSA(public_key);
        if (!rsa)
        {
            return "";
        }
        string out_str(RSA_size(rsa), '\0');
        int ret = 0;
        if (enc_or_dec)
        {
            ret = RSA_public_encrypt(in_str.size(), (uint8_t *)in_str.data(), (uint8_t *)out_str.data(), rsa,
                                     RSA_PKCS1_PADDING);
        }
        else
        {
            ret = RSA_public_decrypt(in_str.size(), (uint8_t *)in_str.data(), (uint8_t *)out_str.data(), rsa,
                                     RSA_PKCS1_PADDING);
        }
        if (ret > 0)
        {
            out_str.resize(ret);
            return out_str;
        }
        WarnL << (enc_or_dec ? "RSA_public_encrypt" : "RSA_public_decrypt") << " failed: " << getLastError();
        return "";
#else
        WarnL << "ENABLE_OPENSSL disabled, you can not use any features based on openssl";
        return "";
#endif // defined(ENABLE_OPENSSL)
    }

    string SSLUtil::cryptWithRsaPrivateKey(EVP_PKEY *private_key, const string &in_str, bool enc_or_dec)
    {
#if defined(ENABLE_OPENSSL)
        auto rsa = EVP_PKEY_get1_RSA(private_key);
        if (!rsa)
        {
            return "";
        }
        string out_str(RSA_size(rsa), '\0');
        int ret = 0;
        if (enc_or_dec)
        {
            ret = RSA_private_encrypt(in_str.size(), (uint8_t *)in_str.data(), (uint8_t *)out_str.data(), rsa,
                                      RSA_PKCS1_PADDING);
        }
        else
        {
            ret = RSA_private_decrypt(in_str.size(), (uint8_t *)in_str.data(), (uint8_t *)out_str.data(), rsa,
                                      RSA_PKCS1_PADDING);
        }
        if (ret > 0)
        {
            out_str.resize(ret);
            return out_str;
        }
        WarnL << getLastError();
        return "";
#else
        WarnL << "ENABLE_OPENSSL disabled, you can not use any features based on openssl";
        return "";
#endif // defined(ENABLE_OPENSSL)
    }

    string SSLUtil::getServerName(X509 *cer)
    {
#if defined(ENABLE_OPENSSL) && defined(SSL_CTRL_SET_TLSEXT_HOSTNAME)
        if (!cer)
        {
            return "";
        }
        // Get domain name from certificate
        X509_NAME *name = X509_get_subject_name(cer);
        char ret[256] = {0};
        X509_NAME_get_text_by_NID(name, NID_commonName, ret, sizeof(ret));
        return ret;
#else
        return "";
#endif
    }

} // namespace toolkit
