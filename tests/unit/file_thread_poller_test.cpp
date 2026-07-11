#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <set>
#include <thread>

#include "Poller/EventPoller.h"
#include "Poller/Pipe.h"
#include "Poller/SelectWrap.h"
#include "Poller/Timer.h"
#include "Thread/TaskQueue.h"
#include "Thread/ThreadPool.h"
#include "Thread/WorkThreadPool.h"
#include "Util/File.h"

using namespace toolkit;

namespace {
std::string tempRoot() {
    return "/tmp/s3toolkit-unit-" + std::to_string(static_cast<unsigned long long>(getCurrentMicrosecond(true)));
}

class ImmediateExecutor : public TaskExecutorInterface {
public:
    Task::Ptr async(TaskIn task, bool = true) override {
        auto result = std::make_shared<Task>(std::move(task));
        (*result)();
        return result;
    }
};
}

TEST(FileTest, SavesLoadsScansAndDeletesTree) {
    const auto root = tempRoot();
    const auto nested = root + "/a/b/data.bin";
    const std::string payload("abc\0def", 7);
    ASSERT_TRUE(File::create_path(nested, 0755));
    ASSERT_TRUE(File::saveFile(payload, nested));
    EXPECT_TRUE(File::fileExist(nested));
    EXPECT_TRUE(File::is_dir(root + "/a"));
    EXPECT_FALSE(File::is_dir(nested));
    EXPECT_EQ(payload, File::loadFile(nested));
    EXPECT_EQ(payload.size(), File::fileSize(nested));
    EXPECT_EQ(root + "/a/b/", File::parentDir(nested));
    EXPECT_TRUE(File::is_special_dir("."));
    EXPECT_TRUE(File::is_special_dir(".."));

    std::set<std::string> entries;
    File::scanDir(root, [&entries](const std::string &path, bool) {
        entries.insert(path);
        return true;
    }, true, true);
    EXPECT_NE(entries.end(), entries.find(nested));

    EXPECT_EQ(0, File::delete_file(root));
    EXPECT_FALSE(File::fileExist(root));
}

TEST(FileTest, SupportsDiskAndMemoryIoInterfaces) {
    auto memory = std::make_shared<FileMemory>("hello");
    auto reader = memory->createReader();
    auto writer = memory->createWriter();
    char buf[8] = {0};
    EXPECT_EQ(0, reader->read(buf, 5));
    EXPECT_STREQ("hello", buf);
    EXPECT_EQ(5, reader->tell());
    EXPECT_EQ(0, writer->seek(0, SEEK_END));
    EXPECT_EQ(0, writer->write("!", 1));
    EXPECT_EQ("hello!", memory->getBuffer());
    EXPECT_EQ(6u, memory->fileSize());
    EXPECT_EQ(0, writer->flush());

    const auto path = tempRoot() + "/disk.txt";
    ASSERT_TRUE(File::create_path(path, 0755));
    auto disk = std::make_shared<FileDisk>();
    disk->openFile(path, "wb+");
    auto disk_writer = disk->createWriter();
    EXPECT_EQ(0, disk_writer->write("world", 5));
    EXPECT_EQ(0, disk_writer->seek(0, SEEK_SET));
    char disk_buf[8] = {0};
    EXPECT_EQ(0, disk->createReader()->read(disk_buf, 5));
    EXPECT_STREQ("world", disk_buf);
    disk->closeFile();
    EXPECT_EQ(0, File::delete_file(File::parentDir(path)));
}

TEST(FileTest, HandlesInvalidPathsOffsetsAndEndOfFile) {
    EXPECT_EQ("", File::loadFile("/tmp/s3toolkit-file-does-not-exist"));
    EXPECT_EQ(0u, File::fileSize("/tmp/s3toolkit-file-does-not-exist"));
    EXPECT_FALSE(File::saveFile("data", "/tmp/s3toolkit-missing-parent/file"));
    EXPECT_FALSE(File::fileExist("/tmp/s3toolkit-file-does-not-exist"));
    EXPECT_FALSE(File::is_dir("/tmp/s3toolkit-file-does-not-exist"));

    auto memory = std::make_shared<FileMemory>("abc");
    auto reader = memory->createReader();
    auto writer = memory->createWriter();
    char data[8] = {0};
    EXPECT_EQ(-1, reader->seek(-1, SEEK_SET));
    EXPECT_EQ(0, reader->seek(99, SEEK_SET));
    EXPECT_EQ(0, reader->seek(0, SEEK_END));
    EXPECT_EQ(-1, reader->read(data, 1));
    EXPECT_EQ(0, writer->seek(-1, SEEK_CUR));
    EXPECT_EQ(0, writer->seek(1, SEEK_END));
    EXPECT_EQ(0, writer->write("z", 1));
    EXPECT_EQ(5u, memory->fileSize());
}

TEST(FileTest, ResolvesPathsSizesHiddenFilesAndEmptyDirectories) {
    EXPECT_EQ("/base/root/", File::absolutePath("", "/base/root/"));
    EXPECT_EQ("/base/root/file", File::absolutePath("./file", "/base/root"));
    EXPECT_EQ("/base/root/", File::absolutePath("../../outside", "/base/root/", false));
    EXPECT_EQ("/base/other/file", File::absolutePath("../other/file", "/base/root/", true));
    EXPECT_EQ("name", File::parentDir("name"));
    EXPECT_EQ("/a/", File::parentDir("/a/b/"));
    EXPECT_EQ(0u, File::fileSize(static_cast<FILE *>(nullptr)));
    EXPECT_EQ(0u, File::fileSize(std::string()));

    const auto root = tempRoot();
    const auto visible = root + "/visible";
    const auto hidden = root + "/.hidden";
    ASSERT_TRUE(File::create_path(root + "/empty/file", 0755));
    ASSERT_TRUE(File::saveFile("abc", visible));
    ASSERT_TRUE(File::saveFile("secret", hidden));
    FILE *fp = fopen(visible.c_str(), "rb");
    ASSERT_NE(nullptr, fp);
    EXPECT_EQ(3u, File::fileSize(fp));
    fseek(fp, 1, SEEK_SET);
    EXPECT_EQ(2u, File::fileSize(fp, true));
    fclose(fp);

    std::vector<std::string> shown;
    File::scanDir(root, [&](const std::string &path, bool) {
        shown.push_back(path);
        return true;
    }, false, false);
    EXPECT_NE(shown.end(), std::find(shown.begin(), shown.end(), visible));
    EXPECT_EQ(shown.end(), std::find(shown.begin(), shown.end(), hidden));
    int calls = 0;
    File::scanDir(root, [&](const std::string &, bool) { ++calls; return false; }, false, true);
    EXPECT_EQ(1, calls);
    EXPECT_NO_THROW(File::scanDir(root + "/missing", [](const std::string &, bool) { return true; }));
    File::deleteEmptyDir(root + "/empty", false);
    EXPECT_FALSE(File::is_dir(root + "/empty"));
    File::deleteEmptyDir(root, false);
    EXPECT_TRUE(File::is_dir(root));
    EXPECT_EQ(0, File::delete_file(root + "/", false));
}

TEST(SelectWrapTest, TracksDescriptorsAndSelectsReadyPipe) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(fds));
    FdSet read;
    read.fdZero();
    read.fdSet(fds[0]);
    EXPECT_TRUE(read.isSet(fds[0]));
    read.fdClr(fds[0]);
    EXPECT_FALSE(read.isSet(fds[0]));
    read.fdSet(fds[0]);
    ASSERT_EQ(1, static_cast<int>(::write(fds[1], "x", 1)));
    timeval timeout{0, 100000};
    EXPECT_EQ(1, zl_select(fds[0] + 1, &read, nullptr, nullptr, &timeout));
    EXPECT_TRUE(read.isSet(fds[0]));
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(TaskTest, CancelableAndQueueHonorOrderingAndExit) {
    TaskCancelableImp<int(int)> task([](int value) { return value * 2; });
    EXPECT_TRUE(static_cast<bool>(task));
    EXPECT_EQ(6, task(3));
    task.cancel();
    EXPECT_FALSE(static_cast<bool>(task));
    EXPECT_EQ(0, task(3));

    TaskQueue<int> queue;
    queue.push_task(2);
    queue.push_task_first(1);
    int value = 0;
    EXPECT_TRUE(queue.get_task(value));
    EXPECT_EQ(1, value);
    EXPECT_TRUE(queue.get_task(value));
    EXPECT_EQ(2, value);
    queue.push_exit(1);
    EXPECT_FALSE(queue.get_task(value));
}

TEST(TaskTest, ExecutorInterfaceRunsSyncAndPriorityFallback) {
    ImmediateExecutor executor;
    int state = 0;
    auto first = executor.async_first([&]() { state = 1; }, false);
    ASSERT_TRUE(first);
    EXPECT_EQ(1, state);
    executor.sync([&]() { state = 2; });
    EXPECT_EQ(2, state);
    executor.sync_first([&]() { state = 3; });
    EXPECT_EQ(3, state);

    TaskCancelableImp<int *()> pointer_task([]() { return static_cast<int *>(nullptr); });
    EXPECT_EQ(nullptr, pointer_task());
    pointer_task = nullptr;
    EXPECT_EQ(nullptr, pointer_task());
    Task void_task([]() {});
    void_task = nullptr;
    EXPECT_NO_THROW(void_task());
}

TEST(ThreadPoolTest, ExecutesAsyncPriorityAndNestedSyncTasks) {
    ThreadPool pool(1, ThreadPool::PRIORITY_NORMAL, false, false, "unit pool");
    std::vector<int> order;
    std::mutex mutex;
    semaphore done;
    pool.async([&]() { std::lock_guard<std::mutex> lock(mutex); order.push_back(2); done.post(); }, false);
    pool.async_first([&]() { std::lock_guard<std::mutex> lock(mutex); order.push_back(1); done.post(); }, false);
    pool.start();
    ASSERT_TRUE(done.wait(1000));
    ASSERT_TRUE(done.wait(1000));
    EXPECT_EQ(std::vector<int>({1, 2}), order);

    semaphore nested_done;
    pool.async([&]() {
        auto result = pool.async([&]() { order.push_back(3); });
        EXPECT_EQ(nullptr, result);
        nested_done.post();
    });
    ASSERT_TRUE(nested_done.wait(1000));
}

TEST(EventPollerTest, RunsAsyncDelayedTimerAndPipeCallbacks) {
    auto poller = EventPollerPool::Instance().getPoller();
    ASSERT_TRUE(poller);
    semaphore async_done;
    bool on_poller = false;
    poller->async([&]() { on_poller = poller->isCurrentThread(); async_done.post(); });
    ASSERT_TRUE(async_done.wait(1000));
    EXPECT_TRUE(on_poller);

    semaphore delayed;
    auto delay_task = poller->doDelayTask(1, [&]() -> uint64_t { delayed.post(); return 0; });
    ASSERT_TRUE(delayed.wait(1000));

    semaphore timer_done;
    auto timer = std::make_shared<Timer>(0.001f, [&]() { timer_done.post(); return false; }, poller);
    ASSERT_TRUE(timer_done.wait(1000));

    semaphore pipe_done;
    std::string received;
    Pipe pipe([&](int size, const char *data) { received.assign(data, size); pipe_done.post(); }, poller);
    pipe.send("ping", 4);
    ASSERT_TRUE(pipe_done.wait(1000));
    EXPECT_EQ("ping", received);
}

TEST(ThreadLoadTest, ReportsBoundedLoadAcrossTransitions) {
    ThreadLoadCounter counter(16, 1000000);
    EXPECT_GE(counter.load(), 0);
    counter.sleepWakeUp();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    counter.startSleep();
    const int load = counter.load();
    EXPECT_GE(load, 0);
    EXPECT_LE(load, 100);
}

TEST(TaskExecutorGetterTest, SelectsEnumeratesAndMeasuresExecutors) {
    WorkThreadPool::setPoolSize(2);
    WorkThreadPool::enableCpuAffinity(false);
    auto &pool = WorkThreadPool::Instance();
    EXPECT_EQ(2u, pool.getExecutorSize());
    EXPECT_TRUE(pool.getFirstPoller());
    EXPECT_TRUE(pool.getPoller());
    EXPECT_TRUE(pool.getExecutor());
    const auto loads = pool.getExecutorLoad();
    EXPECT_EQ(pool.getExecutorSize(), loads.size());

    size_t visited = 0;
    pool.for_each([&visited](const TaskExecutor::Ptr &executor) {
        EXPECT_TRUE(executor);
        ++visited;
    });
    EXPECT_EQ(pool.getExecutorSize(), visited);

    semaphore selected;
    TaskExecutor::Ptr chosen;
    pool.getExecutor([&](const TaskExecutor::Ptr &executor) { chosen = executor; selected.post(); });
    ASSERT_TRUE(selected.wait(1000));
    EXPECT_TRUE(chosen);

    semaphore delayed;
    std::vector<int> delays;
    pool.getExecutorDelay([&](const std::vector<int> &values) { delays = values; delayed.post(); });
    ASSERT_TRUE(delayed.wait(1000));
    EXPECT_EQ(pool.getExecutorSize(), delays.size());
}
