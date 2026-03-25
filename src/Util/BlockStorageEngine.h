#ifndef BLOCKSTORAGEENGINE_H
#define BLOCKSTORAGEENGINE_H

#include "BlockStorage.h"
#include "BlockIndexInterface.h"
#include "Util/logger.h"

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
     * @param block_path Path to the block data file.
     * @param index_path Path to the mmap index file.
     * @param truncate   If true, both files are always created fresh.
     *                   If false (default), existing files are resumed in
     *                   append mode — new blocks are added after existing ones.
     * @return true if both opened successfully.
     */
    bool open(const std::string &block_path, const std::string &index_path,
              bool truncate = false) {
        _writer.openFile(block_path, !truncate);
        if (!_index.openFile(index_path, truncate)) return false;
        if (!truncate) repairIndex();
        return true;
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
     *
     * Safety model:
     *  - flush_after=false: purely buffered — data in stdio buffer, index in
     *    mmap page cache. Fast path for bulk writes.
     *  - flush_after=true: fflush .mblk into OS page cache, add index entry,
     *    msync .idx to disk. Suitable for event-boundary checkpoints.
     *  - On any crash (process kill, OOM), the OS page cache survives;
     *    repairIndex() at open() trims any orphaned index entries.
     *
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
        bool ok = _index.addEntry(entry);
        if (flush_after) _index.flush();
        return ok;
    }

    /**
     * Write a block and atomically record its offset + stamp in the index.
     * ext_header = bytes immediately after BlockHeader on disk (the extension header).
     * payload    = pure payload bytes (ext header excluded).
     *
     * @param flush_after If true: fflush .mblk into page cache, add entry,
     *                    msync .idx to disk. If false: buffered write, fast path.
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
        bool ok = _index.addEntry(entry);
        if (flush_after) _index.flush();
        return ok;
    }

    /**
     * Write a block using a caller-supplied index entry.
     * The caller fills in type-specific fields (e.g. type, flags); this method
     * fills in stamp and offset automatically before adding the entry.
     *
     * @param flush_after If true: fflush .mblk into page cache, add entry,
     *                    msync .idx to disk. If false: buffered write, fast path.
     * @return true if block written and index entry added.
     */
    bool appendBlock(Entry entry,
                     const BlockHeader &header,
                     const uint8_t *ext_header, uint16_t ext_size,
                     const uint8_t *payload, uint32_t payload_size,
                     bool flush_after = false) {
        entry.offset = _writer.position();
        entry.stamp  = header.stamp;
        if (!_writer.appendBlock(header, ext_header, ext_size, payload, payload_size, flush_after)) {
            return false;
        }
        bool ok = _index.addEntry(entry);
        if (flush_after) _index.flush();
        return ok;
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
    /**
     * On resume-open, drop index entries whose recorded offset is at or
     * beyond the actual block file size.  This repairs the inconsistency
     * that can arise when the previous process was killed after the mmap
     * index entry was written but before the stdio buffer was flushed:
     *   index entry visible on disk  ←  mmap page writeback
     *   block bytes NOT on disk      ←  stdio buffer lost on crash
     */
    void repairIndex() {
        const uint64_t blk_size = _writer.position(); // = file size after append-open
        const size_t   n        = _index.getEntryCount();
        if (n == 0) return;

        size_t valid = n;
        while (valid > 0) {
            Entry entry{};
            if (!_index.getEntry(valid - 1, entry)) break;
            // Entry must start at an offset that leaves room for at least
            // a BlockHeader; entries at or beyond file end are orphaned.
            if (entry.offset + sizeof(BlockHeader) <= blk_size) break;
            --valid;
        }
        if (valid < n) {
            WarnL << "BlockStorageWriter: dropped " << (n - valid)
                  << " orphaned index entries (blk_size=" << blk_size
                  << ", first bad offset="
                  << [&]{ Entry e{}; _index.getEntry(valid, e); return e.offset; }()
                  << ")";
            _index.truncateEntries(valid);
            _index.flush();
        }
    }

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
