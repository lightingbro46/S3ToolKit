#ifndef BLOCKINDEXINTERFACE_H
#define BLOCKINDEXINTERFACE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace toolkit {

static const uint32_t INDEX_MAGIC = 0x5844494E; // "INDX"
static const uint16_t INDEX_VERSION = 1;

struct IndexHeader {
    uint32_t magic;         // Magic number to identify the index file
    uint16_t version;       // Version of the index format
    uint16_t entry_size;    // template entry validation
    uint64_t entry_count;   // Number of entries currently in the index
    uint64_t capacity;      // mmap capacity of the index file
    uint64_t reserved[4];   // Reserved for future use
}__attribute__((packed));

struct IndexEntry {
    uint64_t stamp;  // Timestamp of the block
    uint64_t offset; // Offset of the block in the block file
};

class MemoryRegionInterface {
public:
    using Ptr = std::shared_ptr<MemoryRegionInterface>;

    virtual ~MemoryRegionInterface() = default;

    virtual bool open(size_t size) = 0;

    virtual bool resize(size_t new_size) = 0;

    virtual void* data() = 0;

    virtual size_t size() const = 0;

    virtual bool flush(bool sync) = 0;

    virtual void close() = 0;
};

class MMapIndexInterface {
public:
    explicit MMapIndexInterface(size_t entry_size, size_t grow = 1024);

    virtual ~MMapIndexInterface() = default;

    virtual bool addEntry(const uint8_t *entry);

    virtual bool getEntry(size_t index, uint8_t *entry) const;

    virtual bool flush() const;

    virtual size_t getEntryCount() const;

    virtual size_t getCapacity() const;

    size_t getEntrySize() const {
        return _entry_size;
    }

protected:
    size_t getGrow() const {
        return _grow;
    }

    bool bindRegion(const MemoryRegionInterface::Ptr &region);

    void initHeader(size_t capacity);

    bool ensureCapacity(size_t min_capacity);

    bool hasRegion() const {
        return _header != nullptr && _entries != nullptr && _mmap_region;
    }

    IndexHeader *header() {
        return _header;
    }

    const IndexHeader *header() const {
        return _header;
    }

    uint8_t *entries() {
        return _entries;
    }

    const uint8_t *entries() const {
        return _entries;
    }

    MemoryRegionInterface::Ptr &mutableRegion() {
        return _mmap_region;
    }

    virtual MemoryRegionInterface::Ptr remapRegion(size_t mapping_size) = 0;

private:
    size_t _entry_size;
    size_t _grow;
    IndexHeader *_header = nullptr;
    uint8_t *_entries = nullptr;
    MemoryRegionInterface::Ptr _mmap_region;
};

class MMapFileIndex : public MMapIndexInterface {
public:
    explicit MMapFileIndex(size_t entry_size, size_t grow = 1024);

    ~MMapFileIndex() override;

    bool openFile(const std::string &path);

    void closeFile();

private:
    MemoryRegionInterface::Ptr remapRegion(size_t mapping_size) override;

    std::string _file_name;
    int _fd = -1;
};

template <typename Entry>
class TypedMMapFileIndex : public MMapFileIndex {
public:
    explicit TypedMMapFileIndex(size_t grow = 1024)
        : MMapFileIndex(sizeof(Entry), grow) {}

    bool addEntry(const Entry &entry) {
        return MMapFileIndex::addEntry(reinterpret_cast<const uint8_t *>(&entry));
    }

    bool getEntry(size_t index, Entry &entry) const {
        return MMapFileIndex::getEntry(index, reinterpret_cast<uint8_t *>(&entry));
    }
};

} // namespace toolkit


#endif // BLOCKINDEXINTERFACE_H