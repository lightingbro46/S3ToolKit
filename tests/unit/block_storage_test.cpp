#include <gtest/gtest.h>

#include <cstring>

#include "Util/BlockStorageEngine.h"
#include "Util/File.h"

using namespace toolkit;

namespace {
std::string blockTempRoot() {
    return "/tmp/s3toolkit-block-" + std::to_string(static_cast<unsigned long long>(getCurrentMicrosecond(true)));
}

class FailingIo : public FileIOInterface {
public:
    int onRead(void *, size_t) override { return read_result; }
    int onWrite(const void *, size_t) override { return writes++ == fail_write_at ? -1 : 0; }
    int onSeek(int64_t, int) override { return seek_result; }
    int64_t onTell() override { return 0; }
    int onFlush() override { return flush_result; }
    int read_result = -1;
    int seek_result = -1;
    int flush_result = -1;
    int fail_write_at = 0;
    int writes = 0;
};

class MockBlockWriter : public BlockWriterInterface {
public:
    explicit MockBlockWriter(const std::shared_ptr<FailingIo> &io) : io(io) {}
protected:
    FileIOInterface::Writer createWriter() override { return io->createWriter(); }
private:
    std::shared_ptr<FailingIo> io;
};

class MockBlockReader : public BlockReaderInterface {
public:
    explicit MockBlockReader(const std::shared_ptr<FailingIo> &io) : io(io) {}
protected:
    FileIOInterface::Reader createReader() override { return io->createReader(); }
private:
    std::shared_ptr<FailingIo> io;
};

class VectorRegion : public MemoryRegionInterface {
public:
    bool open(size_t size) override { bytes.assign(size, 0); return size != 0; }
    bool resize(size_t size) override {
        if (fail_resize || size == 0) return false;
        bytes.resize(size);
        return true;
    }
    void *data() override { return bytes.empty() ? nullptr : bytes.data(); }
    size_t size() const override { return bytes.size(); }
    bool flush(bool) override { return flush_result; }
    void close() override { bytes.clear(); }
    std::vector<uint8_t> bytes;
    bool fail_resize = false;
    bool flush_result = true;
};

class MemoryIndex : public MMapIndexInterface {
public:
    MemoryIndex(size_t entry_size, size_t grow) : MMapIndexInterface(entry_size, grow) {}
    using MMapIndexInterface::bindRegion;
    using MMapIndexInterface::ensureCapacity;
    using MMapIndexInterface::initHeader;
    using MMapIndexInterface::hasRegion;
    std::shared_ptr<VectorRegion> region = std::make_shared<VectorRegion>();
protected:
    MemoryRegionInterface::Ptr remapRegion(size_t mapping_size) override {
        if (!region->resize(mapping_size)) return nullptr;
        return region;
    }
};
}

TEST(BlockBufferTest, AppendsClearsAndBuildsHeaders) {
    BlockBuffer buffer;
    buffer.reserve(32);
    const uint32_t value = 0x12345678;
    buffer.append(&value, sizeof(value));
    EXPECT_EQ(sizeof(value), buffer.size());
    EXPECT_EQ(0, memcmp(buffer.data(), &value, sizeof(value)));
    buffer.clear();
    EXPECT_EQ(0u, buffer.size());
}

TEST(BlockIndexTest, AddsRetrievesResizesFlushesAndReopensEntries) {
    const auto root = blockTempRoot();
    const auto path = root + "/data.idx";
    ASSERT_TRUE(File::create_path(path, 0755));
    {
        TypedMMapFileIndex<IndexEntry> index(2);
        ASSERT_TRUE(index.openFile(path, true));
        EXPECT_EQ(0u, index.getEntryCount());
        for (uint64_t i = 0; i < 7; ++i) {
            ASSERT_TRUE(index.addEntry(IndexEntry{i * 10, i * 100}));
        }
        EXPECT_EQ(7u, index.getEntryCount());
        EXPECT_GE(index.getCapacity(), 7u);
        IndexEntry entry{};
        ASSERT_TRUE(index.getEntry(4, entry));
        EXPECT_EQ(40u, entry.stamp);
        EXPECT_EQ(400u, entry.offset);
        EXPECT_FALSE(index.getEntry(99, entry));
        EXPECT_TRUE(index.flush());
        EXPECT_TRUE(index.truncateEntries(5));
        EXPECT_FALSE(index.truncateEntries(6));
        index.closeFile();
    }
    {
        TypedMMapFileIndex<IndexEntry> index(2);
        ASSERT_TRUE(index.openFile(path, false));
        EXPECT_EQ(5u, index.getEntryCount());
        IndexEntry entry{};
        ASSERT_TRUE(index.getEntry(0, entry));
        EXPECT_EQ(0u, entry.offset);
        index.closeFile();
    }
    EXPECT_EQ(0, File::delete_file(root));
}

TEST(BlockIndexTest, RejectsMissingTruncatedAndCorruptIndexFiles) {
    const auto root = blockTempRoot();
    const auto missing = root + "/missing/data.idx";
    File::create_path(missing, 0755);
    TypedMMapFileIndex<IndexEntry> index(2);
    EXPECT_TRUE(index.openFile(missing, false));
    index.closeFile();

    ASSERT_TRUE(File::saveFile("bad", missing));
    EXPECT_FALSE(index.openFile(missing, false));
    index.closeFile();
    EXPECT_TRUE(index.openFile(missing, true));
    IndexEntry empty{};
    EXPECT_FALSE(index.getEntry(0, empty));
    index.closeFile();

    std::string corrupt(sizeof(IndexHeader), '\0');
    auto *header = reinterpret_cast<IndexHeader *>(&corrupt[0]);
    header->magic = 0;
    header->version = INDEX_VERSION;
    header->entry_size = sizeof(IndexEntry);
    ASSERT_TRUE(File::saveFile(corrupt, missing));
    EXPECT_TRUE(index.openFile(missing, false));
    index.closeFile();
    File::delete_file(root);
}

TEST(BlockIndexTest, InMemoryRegionCoversValidationCapacityAndFailurePaths) {
    EXPECT_THROW(MemoryIndex(0, 1), std::invalid_argument);
    EXPECT_THROW(MemoryIndex(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1, 1), std::invalid_argument);
    MemoryIndex index(sizeof(IndexEntry), 2);
    EXPECT_FALSE(index.hasRegion());
    EXPECT_FALSE(index.ensureCapacity(1));
    EXPECT_THROW(index.initHeader(1), std::runtime_error);
    EXPECT_TRUE(index.bindRegion(nullptr));
    EXPECT_FALSE(index.addEntry(nullptr));
    EXPECT_FALSE(index.getEntry(0, nullptr));
    EXPECT_FALSE(index.flush());
    EXPECT_FALSE(index.truncateEntries(0));
    EXPECT_EQ(0u, index.getEntryCount());
    EXPECT_EQ(0u, index.getCapacity());

    auto too_small = std::make_shared<VectorRegion>();
    too_small->open(sizeof(IndexHeader) - 1);
    EXPECT_FALSE(index.bindRegion(too_small));
    ASSERT_TRUE(index.region->open(sizeof(IndexHeader) + 2 * sizeof(IndexEntry)));
    ASSERT_TRUE(index.bindRegion(index.region));
    index.initHeader(2);
    EXPECT_TRUE(index.ensureCapacity(1));
    IndexEntry entry{1, 2};
    EXPECT_TRUE(index.addEntry(reinterpret_cast<const uint8_t *>(&entry)));
    EXPECT_FALSE(index.getEntry(1, reinterpret_cast<uint8_t *>(&entry)));
    index.region->flush_result = false;
    EXPECT_FALSE(index.flush());
    EXPECT_FALSE(index.truncateEntries(2));

    index.region->fail_resize = true;
    EXPECT_FALSE(index.ensureCapacity(3));
    index.region->fail_resize = false;
    EXPECT_TRUE(index.ensureCapacity(5));
    EXPECT_GE(index.getCapacity(), 5u);
    EXPECT_TRUE(index.bindRegion(nullptr));
    EXPECT_FALSE(index.hasRegion());
}

TEST(BlockStorageTest, WritesIndexesAndReadsPayloadAndExtensionHeaders) {
    const auto root = blockTempRoot();
    const auto block_path = root + "/data.blk";
    const auto index_path = root + "/data.idx";
    ASSERT_TRUE(File::create_path(block_path, 0755));

    BlockHeader first{0x424c4b31, 1, static_cast<uint16_t>(sizeof(BlockHeader)), 3, 100, 0};
    BlockHeader second{0x424c4b31, 2, static_cast<uint16_t>(sizeof(BlockHeader) + 2), 4, 200, 0};
    const uint8_t payload1[] = {'o', 'n', 'e'};
    const uint8_t ext[] = {9, 8};
    const uint8_t payload2[] = {'t', 'w', 'o', '!'};

    {
        BlockStorageWriter<> writer(2);
        ASSERT_TRUE(writer.open(block_path, index_path, true));
        ASSERT_TRUE(writer.appendBlock(first, payload1, sizeof(payload1), true));
        ASSERT_TRUE(writer.appendBlock(second, ext, sizeof(ext), payload2, sizeof(payload2), true));
        EXPECT_EQ(2u, writer.entryCount());
        EXPECT_EQ(sizeof(first) + sizeof(payload1) + sizeof(second) + sizeof(ext) + sizeof(payload2), writer.position());
        IndexEntry entry{};
        ASSERT_TRUE(writer.getIndexEntry(1, entry));
        EXPECT_EQ(200u, entry.stamp);
        EXPECT_EQ(sizeof(first) + sizeof(payload1), entry.offset);
        EXPECT_TRUE(writer.flush());
        writer.close();
    }

    {
        BlockStorageReader<> reader;
        ASSERT_TRUE(reader.open(block_path, index_path));
        EXPECT_EQ(2u, reader.entryCount());
        BlockHeader header{};
        std::vector<uint8_t> payload;
        bool eof = false;
        ASSERT_TRUE(reader.readBlockAt(0, header, payload, eof));
        EXPECT_EQ(100u, header.stamp);
        EXPECT_EQ(std::vector<uint8_t>(payload1, payload1 + sizeof(payload1)), payload);
        std::vector<uint8_t> extension;
        ASSERT_TRUE(reader.readBlockAt(1, header, extension, payload, eof));
        EXPECT_EQ(std::vector<uint8_t>(ext, ext + sizeof(ext)), extension);
        EXPECT_EQ(std::vector<uint8_t>(payload2, payload2 + sizeof(payload2)), payload);
        EXPECT_FALSE(reader.readBlockAt(99, header, payload, eof));
        reader.close();
    }

    {
        BlockStorageWriter<> writer(2);
        ASSERT_TRUE(writer.open(block_path, index_path, false));
        EXPECT_EQ(2u, writer.entryCount());
        ASSERT_TRUE(writer.appendBlock(first, payload1, sizeof(payload1), false));
        EXPECT_EQ(3u, writer.entryCount());
        writer.close();
    }
    EXPECT_EQ(0, File::delete_file(root));
}

TEST(BlockStorageTest, PropagatesWriterReaderAndFlushFailures) {
    BlockHeader header{0x424c4b31, 1, static_cast<uint16_t>(sizeof(BlockHeader)), 1, 1, 0};
    const uint8_t byte = 1;

    auto io = std::make_shared<FailingIo>();
    MockBlockWriter writer(io);
    EXPECT_TRUE(writer.flush());
    EXPECT_FALSE(writer.appendBlock(header, &byte, 1));
    io->writes = 0;
    io->fail_write_at = 1;
    EXPECT_FALSE(writer.appendBlock(header, &byte, 1));
    io->writes = 0;
    io->fail_write_at = 2;
    EXPECT_TRUE(writer.appendBlock(header, &byte, 1));
    EXPECT_FALSE(writer.flush());

    io->writes = 0;
    io->fail_write_at = 1;
    EXPECT_FALSE(writer.appendBlock(header, &byte, 1, &byte, 1));
    io->writes = 0;
    io->fail_write_at = 2;
    EXPECT_FALSE(writer.appendBlock(header, &byte, 1, &byte, 1));

    auto read_io = std::make_shared<FailingIo>();
    MockBlockReader reader(read_io);
    EXPECT_FALSE(reader.seek(10));
    bool eof = false;
    std::vector<uint8_t> payload;
    EXPECT_FALSE(reader.readBlock(header, payload, eof));
    EXPECT_TRUE(eof);
    std::vector<uint8_t> extension;
    eof = false;
    EXPECT_FALSE(reader.readBlock(header, extension, payload, eof));
    EXPECT_TRUE(eof);
}

TEST(BlockStorageTest, ResumeRepairsOrphanedIndexEntries) {
    const auto root = blockTempRoot();
    const auto block_path = root + "/repair.blk";
    const auto index_path = root + "/repair.idx";
    ASSERT_TRUE(File::create_path(block_path, 0755));
    ASSERT_TRUE(File::saveFile("", block_path));
    {
        TypedMMapFileIndex<IndexEntry> index(2);
        ASSERT_TRUE(index.openFile(index_path, true));
        ASSERT_TRUE(index.addEntry(IndexEntry{1, 9999}));
        ASSERT_TRUE(index.flush());
    }
    {
        BlockStorageWriter<> writer(2);
        ASSERT_TRUE(writer.open(block_path, index_path, false));
        EXPECT_EQ(0u, writer.entryCount());
        writer.close();
    }
    EXPECT_EQ(0, File::delete_file(root));
}
