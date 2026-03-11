#ifndef BLOCKSTORAGEENGINE_H
#define BLOCKSTORAGEENGINE_H

#include "BlockStorage.h"
#include "BlockIndexInterface.h"

namespace toolkit {

/**
 * Combines FileBlockWriter and TypedMMapFileIndex<IndexEntry> to provide
 * automatic index tracking when writing blocks. Each appendBlock call
 * records the block's file offset and timestamp in the mmap'd index,
 * ensuring the block file and index are always in sync.
 *
 * Usage:
 *   BlockStorageEngine engine;
 *   engine.open("data.blk", "data.idx");
 *   engine.appendBlock(header, payload, size);
 *   engine.close();
 */
class BlockStorageEngine {
public:
    explicit BlockStorageEngine(size_t index_grow = 1024)
        : _index(index_grow) {}

    ~BlockStorageEngine() {
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
        IndexEntry entry;
        entry.offset = _writer.position();
        entry.stamp  = header.stamp;

        if (!_writer.appendBlock(header, payload, payloadSize, flush_after)) {
            return false;
        }
        return _index.addEntry(entry);
    }

    /**
     * Retrieve a stored index entry by position.
     */
    bool getIndexEntry(size_t i, IndexEntry &entry) const {
        return _index.getEntry(i, entry);
    }

    size_t entryCount() const {
        return _index.getEntryCount();
    }

    uint64_t position() const {
        return _writer.position();
    }

    // Direct access for advanced use cases
    FileBlockWriter              &writer() { return _writer; }
    TypedMMapFileIndex<IndexEntry> &index()  { return _index;  }

private:
    FileBlockWriter               _writer;
    TypedMMapFileIndex<IndexEntry> _index;
};

} // namespace toolkit

#endif // BLOCKSTORAGEENGINE_H
