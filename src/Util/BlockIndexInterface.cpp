#include "BlockIndexInterface.h"
#include "File.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace toolkit {

namespace {

#if defined(_WIN32)

class AnonymousMemoryRegion : public MemoryRegionInterface {
public:
    ~AnonymousMemoryRegion() override { close(); }
    bool open(size_t size) override;
    bool resize(size_t new_size) override;
    void* data() override { return _data; }
    size_t size() const override { return _size; }
    bool flush(bool sync) override { return true; }
    void close() override;

private:
    void *_data = nullptr;
    size_t _size = 0;
};

bool AnonymousMemoryRegion::open(size_t size) {
    if (size == 0) return false;
    _data = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!_data) return false;
    _size = size;
    std::memset(_data, 0, _size);
    return true;
}

bool AnonymousMemoryRegion::resize(size_t new_size) {
    if (new_size == 0) return false;
    void *new_data = VirtualAlloc(nullptr, new_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!new_data) return false;
    if (_data) {
        std::memcpy(new_data, _data, std::min(_size, new_size));
        VirtualFree(_data, 0, MEM_RELEASE);
    }
    _data = new_data;
    _size = new_size;
    return true;
}

void AnonymousMemoryRegion::close() {
    if (_data) {
        VirtualFree(_data, 0, MEM_RELEASE);
        _data = nullptr;
    }
    _size = 0;
}

#else

class AnonymousMemoryRegion : public MemoryRegionInterface {
public:
    ~AnonymousMemoryRegion() override { close(); }
    bool open(size_t size) override;
    bool resize(size_t new_size) override;
    void* data() override { return _data; }
    size_t size() const override { return _size; }
    bool flush(bool sync) override { return true; }
    void close() override;

private:
    void *_data = nullptr;
    size_t _size = 0;
};

bool AnonymousMemoryRegion::open(size_t size) {
    if (size == 0) return false;
    void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (ptr == MAP_FAILED) return false;
    _data = ptr;
    _size = size;
    std::memset(_data, 0, _size);
    return true;
}

bool AnonymousMemoryRegion::resize(size_t new_size) {
    if (new_size == 0) return false;
    if (!_data) return open(new_size);
    void *ptr = mremap(_data, _size, new_size, MREMAP_MAYMOVE);
    if (ptr == MAP_FAILED) return false;
    _data = ptr;
    _size = new_size;
    return true;
}

void AnonymousMemoryRegion::close() {
    if (_data) {
        munmap(_data, _size);
        _data = nullptr;
    }
    _size = 0;
}

class FileMemoryRegion : public MemoryRegionInterface {
public:
    explicit FileMemoryRegion(int fd) : _fd(fd) {}
    ~FileMemoryRegion() override { close(); }
    bool open(size_t size) override;
    bool resize(size_t new_size) override;
    void* data() override { return _data; }
    size_t size() const override { return _size; }
    bool flush(bool sync) override;
    void close() override;

private:
    int _fd = -1;
    void *_data = nullptr;
    size_t _size = 0;
};

bool FileMemoryRegion::open(size_t size) {
    if (_fd < 0 || size == 0) return false;
    if (ftruncate(_fd, static_cast<off_t>(size)) != 0) return false;
    void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
    if (ptr == MAP_FAILED) return false;
    _data = ptr;
    _size = size;
    return true;
}

bool FileMemoryRegion::resize(size_t new_size) {
    if (_fd < 0 || new_size == 0) return false;
    if (!_data) return open(new_size);
    if (ftruncate(_fd, static_cast<off_t>(new_size)) != 0) return false;
    void *ptr = mremap(_data, _size, new_size, MREMAP_MAYMOVE);
    if (ptr == MAP_FAILED) return false;
    _data = ptr;
    _size = new_size;
    return true;
}

bool FileMemoryRegion::flush(bool sync) {
    if (!_data || _size == 0) return false;
    int flags = sync ? MS_SYNC : MS_ASYNC;
    return msync(_data, _size, flags) == 0;
}

void FileMemoryRegion::close() {
    if (_data) {
        munmap(_data, _size);
        _data = nullptr;
    }
    _size = 0;
}

#endif

size_t checkedMappingSize(size_t entry_size, size_t capacity) {
    if (entry_size == 0 || capacity > (std::numeric_limits<size_t>::max() - sizeof(IndexHeader)) / entry_size) {
        return 0;
    }
    return sizeof(IndexHeader) + entry_size * capacity;
}

} // namespace

MMapIndexInterface::MMapIndexInterface(size_t entry_size, size_t grow)
    : _entry_size(entry_size),
      _grow(std::max<size_t>(1, grow)) {
    if (_entry_size == 0) {
        throw std::invalid_argument("entry_size must be greater than zero");
    }
    if (_entry_size > std::numeric_limits<uint16_t>::max()) {
        throw std::invalid_argument("entry_size exceeds on-disk header limit");
    }
}

bool MMapIndexInterface::bindRegion(const MemoryRegionInterface::Ptr &region) {
    if (!region) {
        if (_mmap_region) {
            _mmap_region->close();
        }
        _mmap_region.reset();
        _header = nullptr;
        _entries = nullptr;
        return true;
    }

    if (region->size() < sizeof(IndexHeader)) {
        return false;
    }

    _mmap_region = region;
    _header = reinterpret_cast<IndexHeader *>(region->data());
    _entries = static_cast<uint8_t *>(region->data()) + sizeof(IndexHeader);
    return true;
}

void MMapIndexInterface::initHeader(size_t capacity) {
    if (!hasRegion()) {
        throw std::runtime_error("index region is not mapped");
    }

    std::memset(_header, 0, sizeof(IndexHeader));
    _header->magic = INDEX_MAGIC;
    _header->version = INDEX_VERSION;
    _header->entry_size = static_cast<uint16_t>(_entry_size);
    _header->entry_count = 0;
    _header->capacity = capacity;
}

bool MMapIndexInterface::ensureCapacity(size_t min_capacity) {
    if (!hasRegion()) {
        return false;
    }
    if (_header->capacity >= min_capacity) {
        return true;
    }

    size_t next_capacity = std::max<size_t>(_grow, static_cast<size_t>(_header->capacity));
    while (next_capacity < min_capacity) {
        if (next_capacity > std::numeric_limits<size_t>::max() - _grow) {
            return false;
        }
        next_capacity += _grow;
    }

    size_t new_mapping_size = checkedMappingSize(_entry_size, next_capacity);
    if (new_mapping_size == 0) return false;

    auto region = remapRegion(new_mapping_size);
    if (!bindRegion(region)) {
        return false;
    }
    _header->capacity = next_capacity;
    return true;
}

bool MMapIndexInterface::addEntry(const uint8_t *entry) {
    if (!entry || !hasRegion()) {
        return false;
    }
    if (!ensureCapacity(static_cast<size_t>(_header->entry_count + 1))) {
        return false;
    }

    std::memcpy(_entries + _header->entry_count * _entry_size, entry, _entry_size);
    ++_header->entry_count;
    return true;
}

bool MMapIndexInterface::getEntry(size_t index, uint8_t *entry) const {
    if (!entry || !hasRegion() || index >= _header->entry_count) {
        return false;
    }

    std::memcpy(entry, _entries + index * _entry_size, _entry_size);
    return true;
}

bool MMapIndexInterface::flush() const {
    return hasRegion() && _mmap_region->flush(true);
}

bool MMapIndexInterface::truncateEntries(size_t new_count) {
    if (!hasRegion()) return false;
    if (new_count > static_cast<size_t>(_header->entry_count)) return false;
    _header->entry_count = static_cast<uint64_t>(new_count);
    return true;
}

size_t MMapIndexInterface::getEntryCount() const {
    return hasRegion() ? static_cast<size_t>(_header->entry_count) : 0;
}

size_t MMapIndexInterface::getCapacity() const {
    return hasRegion() ? static_cast<size_t>(_header->capacity) : 0;
}

MMapFileIndex::MMapFileIndex(size_t entry_size, size_t grow)
    : MMapIndexInterface(entry_size, grow) {}

MMapFileIndex::~MMapFileIndex() {
    closeFile();
}

bool MMapFileIndex::openFile(const std::string &path, bool truncate) {
    if (path.empty()) {
        return false;
    }

    closeFile();
    File::create_path(path, 0777);
    _file_name = path;

#if defined(_WIN32)
    throw std::runtime_error("MMapFileIndex is not implemented on Windows");
#else
    int flags = O_RDWR | O_CREAT | (truncate ? O_TRUNC : 0);
    _fd = ::open(path.c_str(), flags, 0666);
    if (_fd < 0) {
        return false;
    }

    struct stat st;
    if (fstat(_fd, &st) != 0) {
        closeFile();
        return false;
    }

    const size_t initial_capacity = std::max<size_t>(1, getGrow());
    const bool new_file = (st.st_size == 0);
    
    auto region = std::make_shared<FileMemoryRegion>(_fd);
    size_t initial_size = new_file ? checkedMappingSize(getEntrySize(), initial_capacity) : st.st_size;
    
    if (!region->open(initial_size)) {
        closeFile();
        return false;
    }

    if (!bindRegion(region)) {
        closeFile();
        return false;
    }

    if (new_file || header()->magic == 0) {
        initHeader(initial_capacity);
        return flush();
    }

    if (header()->magic != INDEX_MAGIC || header()->version != INDEX_VERSION || header()->entry_size != getEntrySize()) {
        closeFile();
        return false;
    }

    if (header()->entry_count > header()->capacity) {
        closeFile();
        return false;
    }

    return true;
#endif
}

void MMapFileIndex::closeFile() {
    flush();
    _file_name.clear();

#if !defined(_WIN32)
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
#endif

    bindRegion(nullptr);
}

MemoryRegionInterface::Ptr MMapFileIndex::remapRegion(size_t mapping_size) {
#if defined(_WIN32)
    throw std::runtime_error("MMapFileIndex is not implemented on Windows");
#else
    if (_fd < 0) {
        throw std::runtime_error("index file is not open");
    }
    
    auto &region = mutableRegion();
    if (!region->resize(mapping_size)) {
        throw std::runtime_error("failed to resize index region");
    }
    return region;
#endif
}

} // namespace toolkit