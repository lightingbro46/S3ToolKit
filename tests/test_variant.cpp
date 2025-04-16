#include "Util/logger.h"
#include "Util/mini.h"

using namespace toolkit;

int main() {
    // Set log
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    mINI ini;
    ini["a"] = "true";
    ini["b"] = "false";
    ini["c"] = "123";

    InfoL << ini["a"].as<bool>() << (bool) ini["a"];
    InfoL << ini["b"].as<bool>() << (bool) ini["b"];
    InfoL << ini["c"].as<int>() << (int) ini["c"];
    InfoL << (int)(ini["c"].as<uint8_t>()) << (int)((uint8_t) ini["c"]);

    return 0;
}
