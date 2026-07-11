#include <gtest/gtest.h>

#include "Util/SSLBox.h"
#include "Util/SSLUtil.h"

using namespace toolkit;

#if !defined(ENABLE_OPENSSL)
TEST(SslDisabledTest, UtilityFunctionsReturnSafeFallbacks) {
    EXPECT_EQ("No error", SSLUtil::getLastError());
    EXPECT_TRUE(SSLUtil::loadPublicKey("not-a-certificate", "", false).empty());
    EXPECT_FALSE(SSLUtil::loadPrivateKey("not-a-key", "", false));
    EXPECT_FALSE(SSLUtil::makeSSLContext({}, nullptr, false, false));
    EXPECT_FALSE(SSLUtil::makeSSL(nullptr));
    EXPECT_FALSE(SSLUtil::loadDefaultCAs(nullptr));
    EXPECT_FALSE(SSLUtil::trustCertificate(nullptr, nullptr));
    EXPECT_FALSE(SSLUtil::verifyX509(nullptr, nullptr));
    EXPECT_TRUE(SSLUtil::cryptWithRsaPublicKey(nullptr, "data", true).empty());
    EXPECT_TRUE(SSLUtil::cryptWithRsaPrivateKey(nullptr, "data", false).empty());
    EXPECT_TRUE(SSLUtil::getServerName(nullptr).empty());
    EXPECT_TRUE(SSLUtil::cryptWithAes("0123456789abcdef", "0123456789abcdef", "data", true).empty());
}

TEST(SslDisabledTest, BoxPassesPlaintextThroughCallbacks) {
    SSL_Box box(false, true);
    std::string encrypted;
    std::string decrypted;
    box.setOnEncData([&](const Buffer::Ptr &buffer) { encrypted = buffer->toString(); });
    box.setOnDecData([&](const Buffer::Ptr &buffer) { decrypted = buffer->toString(); });
    box.onSend(std::make_shared<BufferString>("plain-send"));
    box.onRecv(std::make_shared<BufferString>("plain-recv"));
    EXPECT_EQ("plain-send", encrypted);
    EXPECT_EQ("plain-recv", decrypted);
    box.onSend(std::make_shared<BufferString>(""));
    box.onRecv(std::make_shared<BufferString>(""));
    EXPECT_FALSE(box.setHost("localhost"));
    EXPECT_NO_THROW(box.flush());
    EXPECT_NO_THROW(box.shutdown());
}

TEST(SslDisabledTest, InitializerRejectsCertificatesAndContextsSafely) {
    auto &initor = SSL_Initor::Instance();
    initor.ignoreInvalidCertificate(false);
    EXPECT_FALSE(initor.loadCertificate("invalid", true, "", false));
    EXPECT_TRUE(initor.trustCertificate("invalid", false, "", false));
    EXPECT_FALSE(initor.trustCertificate(static_cast<X509 *>(nullptr), false));
    EXPECT_FALSE(initor.getSSLCtx("localhost", false));
    EXPECT_FALSE(initor.getSSLCtx("", true));
    initor.ignoreInvalidCertificate(true);
}
#endif
