#ifndef BLOCKSTORAGEENGINE_H
#define BLOCKSTORAGEENGINE_H

#include "BlockStorage.h"
#include "BlockIndexInterface.h"

namespace toolkit {

/**
 * Combines FileBlockWriter and TypedMMapFileIndex<Entry> to provide
 * automatic index tracking when writing blocks. Each appendBlock call
 * records the block's file offset and timestamp in the mmap'd index,
 * ensuring the block file and index are always in sync.
 *
 * Entry must have at least 'stamp' (uint64_t) and 'offset' (uint64_t) fields.
 * Defaults to toolkit::IndexEntry; pass a custom type to extend the index.
 *
 * Usage:
 *   BlockStorageWriter<> writer;
 *   writer.open("data.blk", "data.idx");
 *   writer.appendBlock(header, ext_header, ext_size, payload, size);
 *   writer.close();
 */
template<typename Entry = IndexEntry>
class BlockStorageWriter {
public:
    explicit BlockStorageWriter(size_t index_grow = 1024)
        : _index(index_grow) {}

    ~BlockStorageWriter() {
        close();
    }

    /**
     * Open block file and index file together.
     * @param block_path Path to the block data file  (created/appended)
     * @param index_path Path to the mmap index file   (created/resumed)
     * @return true if both opened successfully
     */
    bool open(const std::string &block_path, const std::string &index_path) {
        _writer.openFile(block_path);
        return _index.openFile(index_path);
    }

    /**
     * Flush both block stream and index mapping, then close both files.
     */
    void close() {
        _writer.closeFile();
        _index.closeFile();
    }

    /**
     * Flush pending data to disk without closing.
     * @return true if both flushes succeeded
     */
    bool flush() {
        return _writer.flush() && _index.flush();
    }

    /**
     * Write a block and atomically record its offset + stamp in the index.
     * @param flush_after Flush block file after writing (default false)
     * @return true if block written and index entry added
     */
    bool appendBlock(const BlockHeader &header, const uint8_t *payload,
                     uint32_t payloadSize, bool flush_after = false) {
        Entry entry{};
        entry.offset = _writer.position();
        entry.stamp  = header.stamp;

        if (!_writer.appendBlock(header, payload, payloadSize, flush_after)) {
            return false;
        }
        return _index.addEntry(entry);
    }

    /**
     * Write a block and atomically record its offset + stamp in the index.
     * ext_header = bytes immediately after BlockHeader on disk (the extension header).
     * payload    = pure payload bytes (ext header excluded).
     * @param flush_after Flush block file after writing (default false)
     * @return true if block written and index entry added
     */
    bool appendBlock(const BlockHeader &header,
                     const uint8_t *ext_header, uint16_t ext_size,
                     const uint8_t *payload, uint32_t payloadSize,
                     bool flush_after = false) {
        Entry entry{};
        entry.offset = _writer.position();
        entry.stamp  = header.stamp;

        if (!_writer.appendBlock(header, ext_header, ext_size, payload, payloadSize, flush_after)) {
            return false;
        }
        return _index.addEntry(entry);
    }

    /**
     * Retrieve a stored index entry by position.
     */
    bool getIndexEntry(size_t i, Entry &entry) const {
        return _index.getEntry(i, entry);
    }

    size_t entryCount() const {
        return _index.getEntryCount();
    }

    uint64_t position() const {
        return _writer.position();
    }

    // Direct access for advanced use cases
    FileBlockWriter            &writer() { return _writer; }
    TypedMMapFileIndex<Entry>  &index()  { return _index;  }

private:
    FileBlockWriter            _writer;
    TypedMMapFileIndex<Entry>  _index;
};

/**
 * Combines FileBlockReader and TypedMMapFileIndex<Entry> to provide
 * block reading with index lookup support.
 *
 * Typical flow:
 *   BlockStorageReader<> reader;
 *   reader.open("data.blk", "data.idx");
 *   reader.readNextBlock(...);      // sequential read
 *   reader.readBlockAt(10, ...);    // random read by index entry
 *   reader.close();
 */
template<typename Entry = IndexEntry>
class BlockStorageReader {
public:
    explicit BlockStorageReader(size_t index_grow = 1024)
        : _index(index_grow) {}

    ~BlockStorageReader() {
        close();
    }

    bool open(const std::string &block_path, const std::string &index_path) {
        _reader.openFile(block_path);
        return _index.openFile(index_path);
    }

    void close() {
        _reader.closeFile();
        _index.closeFile();
    }

    bool readNextBlock(BlockHeader &header, std::vector<uint8_t> &payload, bool &eof) {
        return _reader.readBlock(header, payload, eof);
    }

    bool readNextBlock(BlockHeader &header,
                       std::vector<uint8_t> &ext_header,
                       std::vector<uint8_t> &payload,
                       bool &eof) {
        return _reader.readBlock(header, ext_header, payload, eof);
    }

    bool getIndexEntry(size_t i, Entry &entry) const {
        return _index.getEntry(i, entry);
    }

    size_t entryCount() const {
        return _index.getEntryCount();
    }

    uint64_t position() const {
        return _reader.position();
    }

    bool seekToEntry(size_t i) {
        Entry entry{};
        if (!_index.getEntry(i, entry)) {
            return false;
        }
        return _reader.seek(entry.offset);
    }

    bool readBlockAt(size_t i, BlockHeader &header, std::vector<uint8_t> &payload, bool &eof) {
        eof = false;
        if (!seekToEntry(i)) {
            return false;
        }
        return _reader.readBlock(header, payload, eof);
    }

    bool readBlockAt(size_t i,
                     BlockHeader &header,
                     std::vector<uint8_t> &ext_header,
                     std::vector<uint8_t> &payload,
                     bool &eof) {
        eof = false;
        if (!seekToEntry(i)) {
            return false;
        }
        return _reader.readBlock(header, ext_header, payload, eof);
    }

    FileBlockReader            &reader() { return _reader; }
    TypedMMapFileIndex<Entry>  &index()  { return _index;  }

private:
    FileBlockReader            _reader;
    TypedMMapFileIndex<Entry>  _index;
};

} // namespace toolkit

#endif // BLOCKSTORAGEENGINE_H
