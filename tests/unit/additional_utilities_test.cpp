#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cerrno>
#include <thread>

#include "Network/Buffer.h"
#include "Thread/semaphore.h"
#include "Util/List.h"
#include "Util/NoticeCenter.h"
#include "Util/QueryBuilder.h"
#include "Util/SpeedStatistic.h"
#include "Util/onceToken.h"
#include "Util/util.h"
#include "Util/uv_errno.h"

using namespace toolkit;

TEST(BufferTest, OffsetSupportsValuesAndSharedPointers) {
    BufferOffset<std::string> value("012345", 2, 3);
    EXPECT_EQ("234", value.toString());
    EXPECT_EQ(3u, value.getCapacity());

    auto source = std::make_shared<std::string>("abcdef");
    BufferOffset<std::shared_ptr<std::string>> shared(source, 1);
    EXPECT_EQ("bcdef", shared.toString());
}

TEST(BufferTest, RawAssignCapacityAndBounds) {
    auto buffer = BufferRaw::create(4);
    EXPECT_GE(buffer->getCapacity(), 4u);
    buffer->assign("abc");
    EXPECT_EQ("abc", buffer->toString());
    EXPECT_THROW(buffer->setSize(buffer->getCapacity() + 1), std::invalid_argument);

    const auto capacity = buffer->getCapacity();
    buffer->setCapacity(1);
    EXPECT_EQ(capacity, buffer->getCapacity());
    buffer->setCapacity(4096);
    EXPECT_EQ(4096u, buffer->getCapacity());
}

TEST(BufferTest, LikeStringEditsVisibleRange) {
    BufferLikeString buffer("abcdef");
    buffer.erase(0, 1).erase(3).append("XY").push_back('Z');
    EXPECT_EQ("bcdXYZ", buffer.toString());
    buffer.insert(1, "!", 1);
    EXPECT_EQ("b!cdXYZ", buffer.toString());
    EXPECT_THROW(buffer.erase(99), std::out_of_range);

    BufferLikeString moved(std::move(buffer));
    EXPECT_EQ("b!cdXYZ", moved.toString());
    moved.erase();
    EXPECT_TRUE(moved.toString().empty());
}

TEST(BufferTest, LikeStringCoversCopyAssignResizeAndBounds) {
    BufferLikeString original("0123456789");
    original.erase(0, 2).erase(5);
    EXPECT_EQ("23456", original.toString());

    BufferLikeString copied(original);
    BufferLikeString assigned;
    assigned = copied;
    EXPECT_EQ("23456", assigned.toString());
    EXPECT_EQ('2', static_cast<const BufferLikeString &>(assigned)[0]);
    assigned[1] = 'X';
    EXPECT_EQ("2X456", assigned.toString());
    EXPECT_THROW(assigned[5], std::out_of_range);
    EXPECT_THROW(static_cast<const BufferLikeString &>(assigned)[9], std::out_of_range);

    assigned.resize(3, '_');
    EXPECT_EQ("2X4", assigned.toString());
    assigned.resize(5, '_');
    EXPECT_EQ("2X4__", assigned.toString());
    assigned.resize(9, '+');
    EXPECT_EQ("2X4__++++", assigned.toString());
    assigned.resize(9);

    BufferLikeString move_assigned;
    move_assigned = std::move(assigned);
    EXPECT_EQ("2X4__++++", move_assigned.toString());
    move_assigned = std::string("string");
    EXPECT_EQ("string", move_assigned.toString());
    move_assigned = "literal";
    EXPECT_EQ("literal", move_assigned.toString());
    move_assigned.reserve(128);
    EXPECT_GE(move_assigned.capacity(), 128u);
    move_assigned.clear();
    EXPECT_TRUE(move_assigned.empty());
}

TEST(BufferTest, LikeStringAssignAppendAndEraseEdgeCases) {
    BufferLikeString buffer("abcdefghij");
    const char *inside = buffer.data() + 2;
    buffer.assign(inside, 4);
    EXPECT_EQ("cdef", buffer.toString());
    EXPECT_THROW(buffer.assign(buffer.data(), 100), std::out_of_range);
    EXPECT_THROW(buffer.erase(0, 5), std::out_of_range);

    buffer.append("", 0);
    buffer.assign("", 0);
    EXPECT_EQ("cdef", buffer.toString());
    buffer.erase(2, 1);
    EXPECT_EQ("cdf", buffer.toString());
    buffer.append(std::string("GH"));
    BufferLikeString suffix("IJ");
    buffer.append(suffix);
    EXPECT_EQ("cdfGHIJ", buffer.toString());
    EXPECT_THROW(buffer.erase(buffer.size()), std::out_of_range);

    BufferLikeString compact("01234567890123456789");
    compact.erase(0, 15);
    compact.append("x");
    EXPECT_EQ("56789x", compact.toString());
}

TEST(QueryBuilderTest, BuildsSelectWithEveryClause) {
    QueryBuilder query;
    query.select({"u.id", "count(*)"}).from("users u")
        .join("roles r ON r.id = u.role_id")
        .leftJoin("teams t ON t.id = u.team_id")
        .rightJoin("orgs o ON o.id = t.org_id")
        .where("u.active = ?", {"1"}).groupBy("u.id").having("count(*) > 0")
        .orderBy("u.id DESC").limit(10).offset(20);
    EXPECT_EQ("SELECT u.id, count(*) FROM users u JOIN roles r ON r.id = u.role_id LEFT JOIN teams t ON t.id = u.team_id RIGHT JOIN orgs o ON o.id = t.org_id WHERE u.active = ? GROUP BY u.id HAVING count(*) > 0 ORDER BY u.id DESC LIMIT 10 OFFSET 20", query.build());
    EXPECT_EQ(std::vector<std::string>({"1"}), query.getParams());
}

TEST(QueryBuilderTest, BuildsInsertUpdateAndDeleteParams) {
    QueryBuilder insert;
    insert.insertInto("users").values({{"name", "Ada"}, {"age", "42"}});
    EXPECT_EQ("INSERT INTO users (name, age) VALUES (?, ?)", insert.build());
    EXPECT_EQ(std::vector<std::string>({"Ada", "42"}), insert.getParams());

    QueryBuilder update;
    update.update("users").set({{"name", "Grace"}, {"age", "85"}}).where("id = ?", {"7"});
    EXPECT_EQ("UPDATE users SET name = ?, age = ? WHERE id = ?", update.build());
    EXPECT_EQ(std::vector<std::string>({"Grace", "85", "7"}), update.getParams());

    QueryBuilder remove;
    remove.deleteFrom("users").where("id = ?", {"7"});
    EXPECT_EQ("DELETE FROM users WHERE id = ?", remove.build());
    EXPECT_EQ(std::vector<std::string>({"7"}), remove.getParams());
}

TEST(NoticeCenterTest, EmitsAndRemovesListeners) {
    NoticeCenter center;
    int tag1 = 0;
    int tag2 = 0;
    int total = 0;
    center.addListener(&tag1, "sum", [&total](int &value) { total += value; });
    center.addListener(&tag2, "sum", [&total](int &value) { total += value * 2; });
    int value = 3;
    EXPECT_EQ(2, center.emitEventSafe("sum", value));
    EXPECT_EQ(9, total);
    center.delListener(&tag1, "sum");
    value = 2;
    EXPECT_EQ(1, center.emitEventSafe("sum", value));
    center.delListener(&tag2);
    value = 1;
    EXPECT_EQ(0, center.emitEventSafe("sum", value));
    center.clearAll();
}

TEST(NoticeCenterTest, HandlesMissingEventsTypeMismatchAndClearAll) {
    NoticeCenter center;
    int tag = 0;
    EXPECT_EQ(0, center.emitEventSafe("missing", 1));
    center.delListener(&tag, "missing");
    center.addListener(&tag, "typed", [](const std::string &) {});
    EXPECT_THROW(center.emitEventSafe("typed", 123), std::exception);
    center.addListener(&tag, "second", [](int &) {});
    center.clearAll();
    int value = 1;
    EXPECT_EQ(0, center.emitEventSafe("typed", value));
    EXPECT_EQ(0, center.emitEventSafe("second", value));
}

TEST(SemaphoreTest, SupportsPostWaitAndTimeout) {
    semaphore sem(1);
    EXPECT_TRUE(sem.wait(1));
    EXPECT_FALSE(sem.wait(5));
    std::thread producer([&sem]() { sem.post(2); });
    producer.join();
    sem.wait();
    EXPECT_TRUE(sem.wait(1));
}

TEST(UtilityTest, ManipulatesStringsAndIdentifiers) {
    EXPECT_EQ(std::vector<std::string>({"a", "b", "c"}), split("a,b,c", ","));
    EXPECT_EQ("hello", trim(std::string(" \thello\r\n")));
    EXPECT_EQ("mixed", strToLower(std::string("MiXeD")));
    EXPECT_EQ("MIXED", strToUpper(std::string("MiXeD")));
    std::string replaced = "one two two";
    replace(replaced, "two", "three");
    EXPECT_EQ("one three three", replaced);
    EXPECT_TRUE(start_with("abcdef", "abc"));
    EXPECT_TRUE(end_with("abcdef", "def"));
    EXPECT_TRUE(isIP("127.0.0.1"));
    EXPECT_FALSE(isIP("999.0.0.1"));
    EXPECT_EQ("00 ff 10 ", hexmem("\x00\xff\x10", 3));
    EXPECT_EQ("00112233-4455-6677-8899-aabbccddeeff", format_guid("00112233445566778899aabbccddeeff"));
    EXPECT_EQ("00112233445566778899aabbccddeeff", format_guid_without_dash("00112233-4455-6677-8899-aabbccddeeff"));
}

TEST(UtilityTest, GeneratesRandomTimePathEnvironmentAndDebugRepresentations) {
    const auto printable = makeRandStr(64, true);
    EXPECT_EQ(64u, printable.size());
    for (char ch : printable) EXPECT_TRUE(std::isalnum(static_cast<unsigned char>(ch)));
    EXPECT_EQ(32u, makeRandStr(32, false).size());
    EXPECT_NE(0u, makeRandNum());
    const auto uuid = makeUuidStr();
    EXPECT_EQ(36u, uuid.size());
    EXPECT_EQ('-', uuid[8]);
    EXPECT_EQ(36u, generate_guid().size());
    EXPECT_EQ("", format_guid("short"));

    const char data[] = "abc\x01";
    const auto dump = hexdump(data, sizeof(data) - 1);
    EXPECT_NE(std::string::npos, dump.find("61 62 63 01"));
    EXPECT_NE(std::string::npos, dump.find("abc."));
    EXPECT_FALSE(exePath().empty());
    EXPECT_FALSE(exeDir().empty());
    EXPECT_FALSE(exeName().empty());
    EXPECT_FALSE(getTimeStr("%Y").empty());
    EXPECT_GT(getCurrentMillisecond(true), 0u);
    EXPECT_GT(getCurrentMicrosecond(true), 0u);
    EXPECT_FALSE(demangle(typeid(std::string).name()).empty());

    setenv("S3TOOLKIT_UNIT_ENV", "present", 1);
    EXPECT_EQ("present", getEnv("S3TOOLKIT_UNIT_ENV"));
    EXPECT_EQ("present", getEnv("$S3TOOLKIT_UNIT_ENV"));
    EXPECT_EQ("", getEnv(""));
    EXPECT_EQ("", getEnv("S3TOOLKIT_MISSING_ENV"));
}

TEST(UtilityTest, CoversStringEdgeCasesAndAssertionHelper) {
    EXPECT_EQ(std::vector<std::string>({""}), split("", ","));
    EXPECT_EQ(std::vector<std::string>({"a", "b"}), split(",a,,b,", ","));
    std::string unchanged = "abc";
    replace(unchanged, "", "x");
    replace(unchanged, "x", "x");
    replace(unchanged, "missing", "x");
    EXPECT_EQ("abc", unchanged);
    EXPECT_FALSE(start_with("abc", "bc"));
    EXPECT_FALSE(end_with("abc", "ab"));
    EXPECT_TRUE(isIP("::1"));
    EXPECT_FALSE(isIP("invalid"));
    EXPECT_NO_THROW(Assert_Throw(0, "true", __FUNCTION__, __FILE__, __LINE__, nullptr));
    EXPECT_THROW(Assert_Throw(1, "false", __FUNCTION__, __FILE__, __LINE__, "details"), AssertFailedException);
}

TEST(UtilityTest, OnceTokenRunsLifecycleCallbacks) {
    int state = 0;
    {
        onceToken token([&state]() { state = 1; }, [&state]() { state = 2; });
        EXPECT_EQ(1, state);
    }
    EXPECT_EQ(2, state);
}

TEST(UtilityTest, ListAppendAndIteration) {
    List<int> left;
    left.push_back(1);
    left.push_back(2);
    List<int> right;
    right.push_back(3);
    right.push_back(4);
    left.append(right);
    EXPECT_TRUE(right.empty());
    int sum = 0;
    left.for_each([&sum](int value) { sum += value; });
    EXPECT_EQ(10, sum);
}

TEST(UtilityTest, UvErrorHelpersMapKnownAndUnknownErrors) {
    EXPECT_EQ(-EINVAL, uv_translate_posix_error(EINVAL));
    EXPECT_EQ(UV_EAGAIN, uv_translate_posix_error(EINPROGRESS));
    EXPECT_STREQ("EINVAL", uv_err_name(UV_EINVAL));
    EXPECT_NE(std::string::npos, std::string(uv_strerror(UV_EINVAL)).find("invalid"));
    EXPECT_NE(std::string::npos, std::string(uv_err_name(-123456)).find("Unknown"));
}

TEST(UtilityTest, SpeedAndTickerExposeMonotonicState) {
    Ticker ticker;
    const auto before = ticker.createdTime();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_GE(ticker.createdTime(), before);
    ticker.resetTime();
    EXPECT_LT(ticker.elapsedTime(), 100u);

    BytesSpeed speed;
    speed += 100;
    EXPECT_EQ(100u, speed.getTotalBytes());
    EXPECT_EQ(0u, speed.getSpeed());

    BytesSpeed bulk;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    bulk += 1024 * 1024 + 1;
    EXPECT_EQ(1024u * 1024u + 1u, bulk.getTotalBytes());
    EXPECT_GE(bulk.getSpeed(), 0u);
}
