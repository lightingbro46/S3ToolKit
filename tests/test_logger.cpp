#include <iostream>
#include "Util/logger.h"
#include "Network/Socket.h"
using namespace std;
using namespace toolkit;

class TestLog
{
public:
    template<typename T>
    TestLog(const T &t){
        _ss << t;
    };
    ~TestLog(){};

    // Through this friend method, you can print custom data types
    friend ostream& operator<<(ostream& out,const TestLog& obj){
        return out << obj._ss.str();
    }
private:
    stringstream _ss;
};

int main() {
    // Initialize the logging system
    Logger::Instance().add(std::make_shared<ConsoleChannel> ());
    Logger::Instance().add(std::make_shared<FileChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    InfoL << "Test std::cout style printing：";
    // All data types supported by ostream are supported, and custom type data can be printed through friend methods
    TraceL << "object int"<< TestLog((int)1)  << endl;
    DebugL << "object short:"<<TestLog((short)2)  << endl;
    InfoL << "object float:" << TestLog((float)3.12345678)  << endl;
    WarnL << "object double:" << TestLog((double)4.12345678901234567)  << endl;
    ErrorL << "object void *:" << TestLog((void *)0x12345678) << endl;
    ErrorL << "object string:" << TestLog("test string") << endl;

    // These are the data types natively supported by ostream
    TraceL << "int"<< (int)1  << endl;
    DebugL << "short:"<< (short)2  << endl;
    InfoL << "float:" << (float)3.12345678  << endl;
    WarnL << "double:" << (double)4.12345678901234567  << endl;
    ErrorL << "void *:" << (void *)0x12345678 << endl;
    // Based on the RAII principle, there is no need to input endl here, and the log will be printed when the function is popped from the stack
    ErrorL << "without endl!";

    PrintI("Test printf style printing：");
    PrintT("this is a %s test:%d", "printf trace", 124);
    PrintD("this is a %s test:%p", "printf debug", (void*)124);
    PrintI("this is a %s test:%c", "printf info", 'a');
    PrintW("this is a %s test:%X", "printf warn", 0x7F);
    PrintE("this is a %s test:%x", "printf err", 0xab);

    LogI("Test variable length template style printing:");
    LogT(1, "+", "2", '=', 3);
    LogD(1, "+", "2", '=', 3);
    LogI(1, "+", "2", '=', 3);
    LogW(1, "+", "2", '=', 3);
    LogE(1, "+", "2", '=', 3);


    for (int i = 0; i < 2; ++i) {
        DebugL << "this is a repeat 2 times log";
        this_thread::sleep_for(chrono::milliseconds(10));
    }

    for (int i = 0; i < 3; ++i) {
        DebugL << "this is a repeat 3 times log";
        this_thread::sleep_for(chrono::milliseconds(10));
    }

    for (int i = 0; i < 100; ++i) {
        DebugL << "this is a repeat 100 log";
        this_thread::sleep_for(chrono::milliseconds(10));
    }

    toolkit::SockException ex((ErrCode)1, "test");
    DebugL << "sock exception: " << ex;

    InfoL << "done!";
    return 0;
}
