#include <csignal>
#include <iostream>
#include "Util/logger.h"
#include "Poller/EventPoller.h"
#include "Poller/Pipe.h"
#include "Util/util.h"
using namespace std;
using namespace toolkit;

int main() {
    // Set up logging
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
#if defined(_WIN32)
    ErrorL << "This test program cannot be run under Windows, because I don't know how to program multi-process under Windows, but the pipeline module can work normally under Windows。" << endl;
#else
    // Get the parent process's PID
    auto parentPid = getpid();
    InfoL << "parent pid:" << parentPid << endl;

    // Define a pipe, with a lambda type parameter as the callback for when data is received
    Pipe pipe([](int size,const char *buf) {
        // The pipe has data available for reading
        InfoL << getpid() << " recv:" << buf;
    });

    // Create a child process
    auto pid = fork();

    if (pid == 0) {
        // Child process
        int i = 10;
        while (i--) {
            // In the child process, write data to the pipe every second, for a total of 10 times
            sleep(1);
            string msg = StrPrinter << "message " << i << " form subprocess:" << getpid();
            DebugL << "Subprocess sending:" << msg << endl;
            pipe.send(msg.data(), msg.size());
        }
        DebugL << "Subprocess exits" << endl;
    } else {
        // Parent process sets up exit signal handling function
        static semaphore sem;
        signal(SIGINT, [](int) { sem.post(); });// Set the exit signal
        sem.wait();

        InfoL << "Parent process exits" << endl;
    }
#endif // defined(_WIN32)

    return 0;
}
