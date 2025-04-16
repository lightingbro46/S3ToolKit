#include <chrono>
#include "Util/logger.h"
#include "Util/onceToken.h"
#include "Util/TimeTicker.h"
#include "Thread/ThreadPool.h"

using namespace std;
using namespace toolkit;

int main() {
    // Initialize the logging system
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    ThreadPool pool(thread::hardware_concurrency(), ThreadPool::PRIORITY_HIGHEST, true);

    // Each task takes 3 seconds
    auto task_second = 3;
    // Each thread executes 4 tasks on average, the total time should be 12 seconds
    auto task_count = thread::hardware_concurrency() * 4;

    semaphore sem;
    vector<int> vec;
    vec.resize(task_count);
    Ticker ticker;
    {
        // Put it in a scope to ensure the token reference count is decremented
        auto token = std::make_shared<onceToken>(nullptr, [&]() {
            sem.post();
        });

        for (auto i = 0; i < task_count; ++i) {
            pool.async([token, i, task_second, &vec]() {
                setThreadName(("thread pool " + to_string(i)).data());
                std::this_thread::sleep_for(std::chrono::seconds(task_second)); //Sleep for three seconds
                InfoL << "task " << i << " done!";
                vec[i] = i;
            });
        }
    }

    sem.wait();
    InfoL << "all task done, used milliseconds:" << ticker.elapsedTime();

    // Print the execution result
    for (auto i = 0; i < task_count; ++i) {
        InfoL << vec[i];
    }
    return 0;
}
