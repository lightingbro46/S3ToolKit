#include <iostream>
#include "Util/logger.h"
#include "Util/util.h"
#include "Util/SSLBox.h"
using namespace std;
using namespace toolkit;

int main(int argc,char *argv[]) {
    // Initialize log settings
    Logger::Instance().add(std::make_shared<ConsoleChannel> ());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    // Load certificate, certificate contains public key and private key
    SSL_Initor::Instance().loadCertificate((exeDir() + "ssl.p12").data());
    SSL_Initor::Instance().trustCertificate((exeDir() + "ssl.p12").data());
    SSL_Initor::Instance().ignoreInvalidCertificate(false);

    // Define client and server
    SSL_Box client(false), server(true);

    // Set client decryption output callback
    client.setOnDecData([&](const Buffer::Ptr &buffer) {
        // Print plaintext from server after decryption
        InfoL << "client recv:" << buffer->toString();
    });

    // Set client encryption output callback
    client.setOnEncData([&](const Buffer::Ptr &buffer) {
        // Send encrypted ciphertext from client to server
        server.onRecv(buffer);
    });

    // Set server decryption output callback
    server.setOnDecData([&](const Buffer::Ptr &buffer) {
        // Print plaintext from client after decryption
        InfoL << "server recv:" << buffer->toString();
        // Echo data back to client
        server.onSend(buffer);
    });

    // Set server-side encryption output callback
    server.setOnEncData([&](const Buffer::Ptr &buffer) {
        // Return the encrypted echo information to the client;
        client.onRecv(buffer);
    });

    InfoL << "Please enter characters to start the test, enter quit to stop the test:" << endl;

    string input;
    while (true) {
        std::cin >> input;
        if (input == "quit") {
            break;
        }
        // Input plaintext data to the client
        client.onSend(std::make_shared<BufferString>(std::move(input)));
    }
    return 0;
}
