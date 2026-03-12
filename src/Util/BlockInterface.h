#ifndef BLOCKINTERFACE_H
#define BLOCKINTERFACE_H

#include "File.h"

namespace toolkit {

#pragma pack(push, 1)

struct BlockHeader {
    uint32_t magic; // Magic number to identify the block, eg. 'BLK1'
    uint16_t type;  // block type, defined by the user
    uint16_t header_size; // header + ext header
    uint32_t payload_size; // Size of the block data
    uint64_t stamp; // Timestamp of when the block was written
    uint32_t crc; // CRC32 checksum of the block data for integrity verification
};

#pragma pack(pop)

/**
 * A simple block buffer class that provides basic operations for managing a block of data in memory,
 * such as reserving space, appending data, and retrieving the buffer content and size
 */
class BlockBuffer {
public:
    void reserve(size_t size) {
        _buf.reserve(size);
    }

    void clear() {
        _buf.clear();
    }

    void append(const void *data, size_t size) {
        const uint8_t *p = reinterpret_cast<const uint8_t *>(data);
        _buf.insert(_buf.end(), p, p + size);
    }

    uint8_t *data() {
        return _buf.data();
    }

    const uint8_t *data() const {
        return _buf.data();
    }

    size_t size() const {
        return _buf.size();
    }   

private:
    std::vector<uint8_t> _buf;
};

/**
 * Interface for block data, which can be implemented by users to define their own block types and data structures
 */
class BlockInterface {
public:
    virtual ~BlockInterface() = default;

    virtual uint32_t magic() const = 0;

    virtual uint16_t type() const = 0;

    virtual uint64_t stamp() const = 0;

    virtual uint32_t headerSize() const = 0;

    virtual uint32_t payloadSize() const = 0;

    virtual uint32_t crc() const = 0;

    virtual void serialize(BlockBuffer &buffer) const = 0;

    /**
     * Size of the extension header in bytes (data between BlockHeader and payload on disk).
     * Equals headerSize() - sizeof(BlockHeader).
     */
    uint16_t extHeaderSize() const {
        return static_cast<uint16_t>(headerSize() - sizeof(BlockHeader));
    }

    /**
     * Build the on-disk BlockHeader for this block.
     * payload_size reflects only the pure payload (ext header is excluded).
     * Total on-disk block size = header_size + payload_size.
     */
    BlockHeader buildBaseHeader() const {
        BlockHeader hdr{};
        hdr.magic        = magic();
        hdr.type         = type();
        hdr.header_size  = static_cast<uint16_t>(headerSize());
        hdr.payload_size = payloadSize();
        hdr.stamp        = stamp();
        hdr.crc          = crc();
        return hdr;
    }
};

} /* namespace toolkit */

#endif // BLOCKINTERFACE_H