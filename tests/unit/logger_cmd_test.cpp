#include <gtest/gtest.h>

#include <sstream>

#include "Util/CMD.h"
#include "Util/File.h"
#include "Util/NoticeCenter.h"
#include "Util/logger.h"

using namespace toolkit;

namespace {
class CaptureChannel : public LogChannel {
public:
    CaptureChannel() : LogChannel("capture", LTrace) {}
    void write(const Logger &logger, const LogContextPtr &ctx) override {
        std::ostringstream stream;
        format(logger, stream, ctx, false, true);
        output += stream.str();
        ++writes;
    }
    std::string output;
    int writes = 0;
    std::string render(const Logger &logger, const LogContextPtr &ctx, bool color, bool detail) {
        std::ostringstream stream;
        format(logger, stream, ctx, color, detail);
        return stream.str();
    }
};

class SampleCMD : public CMD {
public:
    SampleCMD() {
        _parser = std::make_shared<OptionParser>([this](const std::shared_ptr<std::ostream> &, mINI &args) {
            completed = true;
            completed_args = args;
        });
        (*_parser) << Option('n', "name", Option::ArgRequired, nullptr, true, "user name", nullptr)
                   << Option('c', "count", Option::ArgOptional, "3", false, "item count", nullptr)
                   << Option('v', "verbose", Option::ArgNone, nullptr, false, "verbose output",
                             [this](const std::shared_ptr<std::ostream> &, const std::string &) { callback_called = true; return true; });
    }
    const char *description() const override { return "sample command"; }
    bool completed = false;
    bool callback_called = false;
    mINI completed_args;
};
}

TEST(LoggerTest, WritesFormatsFiltersAndRemovesChannels) {
    Logger logger("unit-logger");
    auto capture = std::make_shared<CaptureChannel>();
    logger.add(capture);
    EXPECT_EQ(capture, logger.get("capture"));
    EXPECT_EQ("unit-logger", logger.getName());

    auto context = std::make_shared<LogContext>(LInfo, "/tmp/source.cpp", "function", 42, "module", "flag");
    (*context) << "hello " << 7;
    logger.write(context);
    EXPECT_EQ(1, capture->writes);
    EXPECT_NE(std::string::npos, capture->output.find("hello 7"));
    EXPECT_NE(std::string::npos, capture->output.find("source.cpp:42"));
    EXPECT_EQ("hello 7", context->str());

    logger.setLevel(LError);
    auto ignored = std::make_shared<LogContext>(LDebug, __FILE__, __FUNCTION__, __LINE__, "", "");
    (*ignored) << "ignored";
    logger.write(ignored);
    EXPECT_GE(capture->writes, 1);
    logger.del("capture");
    EXPECT_EQ(nullptr, logger.get("capture"));
}

TEST(LoggerTest, CapturePrintfAndAsyncWriterProduceMessages) {
    Logger logger("async-unit");
    auto capture = std::make_shared<CaptureChannel>();
    logger.add(capture);
    {
        LogContextCapture log(logger, LWarn, __FILE__, __FUNCTION__, __LINE__, "tag");
        log << "captured" << std::endl;
    }
    LoggerWrapper::printLog(logger, LError, __FILE__, __FUNCTION__, __LINE__, "value=%d", 9);
    EXPECT_GE(capture->writes, 2);
    EXPECT_NE(std::string::npos, capture->output.find("captured"));
    EXPECT_NE(std::string::npos, capture->output.find("value=9"));

    capture->output.clear();
    logger.setWriter(std::make_shared<AsyncLogWriter>());
    auto context = std::make_shared<LogContext>(LInfo, __FILE__, __FUNCTION__, __LINE__, "", "");
    (*context) << "async-message";
    logger.write(context);
    for (int i = 0; i < 100 && capture->output.find("async-message") == std::string::npos; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_NE(std::string::npos, capture->output.find("async-message"));
    logger.setWriter(nullptr);
}

TEST(LoggerTest, FileAndEventChannelsPersistAndBroadcast) {
    const auto path = "/tmp/s3toolkit-log-" + std::to_string(static_cast<unsigned long long>(getCurrentMicrosecond(true))) + "/unit.log";
    ASSERT_TRUE(File::create_path(path, 0755));
    Logger logger("channel-unit");
    auto file = std::make_shared<FileChannelBase>("file", path, LTrace);
    EXPECT_TRUE(file->setPath(path));
    EXPECT_EQ(path, file->path());
    logger.add(file);

    int tag = 0;
    int broadcasts = 0;
    NoticeCenter::Instance().addListener(&tag, EventChannel::getBroadcastLogEventName(),
        [&broadcasts](const Logger &, const LogContextPtr &) { ++broadcasts; });
    logger.add(std::make_shared<EventChannel>());
    auto context = std::make_shared<LogContext>(LInfo, __FILE__, __FUNCTION__, __LINE__, "", "");
    (*context) << "file-event-message";
    logger.write(context);
    logger.del("file");
    EXPECT_NE(std::string::npos, File::loadFile(path).find("file-event-message"));
    EXPECT_EQ(1, broadcasts);
    NoticeCenter::Instance().delListener(&tag);
    File::delete_file(File::parentDir(path));
}

TEST(LoggerTest, RotatingFileChannelCreatesAndCleansSlices) {
    const auto dir = "/tmp/s3toolkit-rotating-log-" + std::to_string(static_cast<unsigned long long>(getCurrentMicrosecond(true)));
    File::create_path(dir + "/placeholder", 0755);
    Logger logger("rotation-unit");
    auto channel = std::make_shared<FileChannel>("rotation", dir, LTrace);
    channel->setMaxDay(0);
    channel->setFileMaxSize(0);
    channel->setFileMaxCount(0);
    logger.add(channel);
    auto context = std::make_shared<LogContext>(LWarn, __FILE__, __FUNCTION__, __LINE__, "", "");
    (*context) << "rotating-message";
    logger.write(context);
    logger.del("rotation");
    bool found = false;
    File::scanDir(dir, [&found](const std::string &path, bool is_dir) {
        if (!is_dir && end_with(path, ".log")) found = true;
        return true;
    });
    EXPECT_TRUE(found);
    File::delete_file(dir);
}

TEST(LoggerTest, CoversRepeatFormattingFiltersAndInvalidFilePath) {
    Logger logger("repeat-unit");
    auto capture = std::make_shared<CaptureChannel>();
    logger.add(capture);
    auto first = std::make_shared<LogContext>(LInfo, "repeat.cpp", "fn", 7, "", "");
    (*first) << "same";
    logger.write(first);
    auto repeated = std::make_shared<LogContext>(LInfo, "repeat.cpp", "fn", 7, "", "");
    (*repeated) << "same";
    repeated->_tv.tv_sec = first->_tv.tv_sec + 1;
    logger.write(repeated);
    EXPECT_GE(capture->writes, 1);

    auto empty = std::make_shared<LogContext>(LInfo, "empty.cpp", "fn", 1, "", "");
    EXPECT_TRUE(capture->render(logger, empty, false, false).empty());
    auto rendered = std::make_shared<LogContext>(LInfo, "render.cpp", "fn", 2, "", "");
    rendered->_repeat = 3;
    (*rendered) << "rendered";
    const auto colored = capture->render(logger, rendered, true, true);
    EXPECT_NE(std::string::npos, colored.find("rendered"));
    EXPECT_NE(std::string::npos, colored.find("Last message repeated 3 times"));

    EventChannel event("filtered-event", LError);
    event.write(logger, first);
    ConsoleChannel console("filtered-console", LError);
    console.write(logger, first);
#if defined(__linux__) && !defined(ANDROID)
    SysLogChannel syslog("filtered-syslog", LError);
    syslog.write(logger, first);
    syslog.setLevel(LTrace);
    syslog.write(logger, first);
#endif
    FileChannelBase invalid("invalid", "", LTrace);
    EXPECT_THROW(invalid.setPath(""), std::runtime_error);
    EXPECT_FALSE(invalid.setPath("/proc/s3toolkit-forbidden/unit.log"));

    setLogger(&logger);
    EXPECT_EQ(&logger, &getLogger());
    setLogger(nullptr);
    EXPECT_NE(nullptr, &getLogger());
}

TEST(CmdTest, ParsesRequiredOptionalFlagAndPositionalArguments) {
    auto command = std::make_shared<SampleCMD>();
    char a0[] = "sample";
    char a1[] = "--name";
    char a2[] = "Ada";
    char a3[] = "-v";
    char a4[] = "extra";
    char *argv[] = {a0, a1, a2, a3, a4};
    auto output = std::make_shared<std::ostringstream>();
    (*command)(5, argv, output);
    EXPECT_TRUE(command->completed);
    EXPECT_TRUE(command->callback_called);
    EXPECT_EQ("Ada", (*command)["name"]);
    EXPECT_EQ(3, (*command)["count"].as<int>());
    EXPECT_TRUE(command->hasKey("verbose"));
    EXPECT_FALSE(command->completed_args.empty());
}

TEST(CmdTest, ValidatesMissingUnknownAndHelpOptions) {
    SampleCMD command;
    char a0[] = "sample";
    char *missing[] = {a0};
    EXPECT_THROW(command(1, missing), std::invalid_argument);

    char a1[] = "--unknown";
    char *unknown[] = {a0, a1};
    EXPECT_THROW(command(2, unknown), std::invalid_argument);

    char help[] = "--help";
    char *help_args[] = {a0, help};
    EXPECT_THROW(command(2, help_args), std::invalid_argument);
}

TEST(CmdTest, RegisterDispatchHelpClearAndExitCommands) {
    auto &registry = CMDRegister::Instance();
    registry.clear();
    auto sample = std::make_shared<SampleCMD>();
    registry.registCMD("sample", sample);
    registry.registCMD("help", std::make_shared<CMD_help>());
    registry.registCMD("clear", std::make_shared<CMD_clear>());
    registry.registCMD("exit", std::make_shared<CMD_exit>());
    auto output = std::make_shared<std::ostringstream>();
    registry.printHelp(output);
    EXPECT_NE(std::string::npos, output->str().find("sample command"));
    EXPECT_THROW(registry("missing"), std::invalid_argument);
    EXPECT_THROW(registry("exit"), ExitException);
    registry("clear", output);
    EXPECT_NE(std::string::npos, output->str().find("\x1b[2J"));
    registry.clear();
}
