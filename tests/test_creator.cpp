#include <iostream>
#include "Util/logger.h"
using namespace std;
using namespace toolkit;

// Test when both onCreate and onDestroy exist
class TestA {
public:
    TestA() {
        TraceL;
    }

    ~TestA() {
        TraceL;
    }

    void onCreate() {
        TraceL << demangle(typeid(*this).name()) << "::" << __FUNCTION__;
    }

    void onDestory() {
        TraceL << demangle(typeid(*this).name()) << "::" << __FUNCTION__;
    }
};

// Test when only onCreate exists
class TestB {
public:
    TestB() {
        TraceL;
    }

    ~TestB() {
        TraceL;
    }

    void onCreate() {
        TraceL << demangle(typeid(*this).name()) << "::" << __FUNCTION__;
    }
};

// Test when only onDestroy exists
class TestC {
public:
    TestC() {
        TraceL;
    }

    ~TestC() {
        TraceL;
    }

    void onDestory() {
        TraceL << demangle(typeid(*this).name()) << "::" << __FUNCTION__;
    }
};

// Test when onCreate and onDestroy return values are not void
class TestD {
public:
    TestD() {
        TraceL;
    }

    ~TestD() {
        TraceL;
    }

    int onCreate() {
        TraceL << demangle(typeid(*this).name()) << "::" << __FUNCTION__;
        return 1;
    }

    std::string onDestory() {
        TraceL << demangle(typeid(*this).name()) << "::" << __FUNCTION__;
        return "test";
    }
};

// Test when neither onCreate nor onDestroy exist
class TestE {
public:
    TestE() {
        TraceL;
    }

    ~TestE() {
        TraceL;
    }
};

// Test custom constructor
class TestF {
public:
    TestF(int a, const char *b) {
        TraceL << a << " " << b;
    }

    ~TestF() {
        TraceL;
    }
};

// Test custom onCreate function
class TestH {
public:
    TestH() {
        TraceL;
    }

    int onCreate(int a = 0, const char *b = nullptr) {
        TraceL << demangle(typeid(*this).name()) << "::" << __FUNCTION__ << " " << a << " " << b;
        return 10;
    }

    ~TestH() {
        TraceL;
    }
};

// Test onDestroy function throws an exception
class TestI {
public:
    TestI() {
        TraceL;
    }

    int onDestory() {
        TraceL << demangle(typeid(*this).name()) << "::" << __FUNCTION__;
        throw std::runtime_error("TestI");
    }

    ~TestI() {
        TraceL;
    }
};

// Test custom onDestroy, onDestroy will be ignored when called
class TestJ {
public:
    TestJ() {
        TraceL;
    }

    int onDestory(int a) {
        return a;
    }

    ~TestJ() {
        TraceL;
    }
};

int main() {
    // Initialize the logging system
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    Creator::create<TestA>();
    Creator::create<TestB>();
    Creator::create<TestC>();
    Creator::create<TestD>();
    Creator::create<TestE>();
    Creator::create<TestF>(1, "hellow");
    {
        auto h = Creator::create2<TestH>(1, "hellow");
        TraceL << "invoke TestH onCreate ret:" << CLASS_FUNC_INVOKE(TestH, *h, Create, 1, "hellow");
    }

    Creator::create<TestI>();
    Creator::create<TestJ>();
    return 0;
}
