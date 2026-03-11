#ifndef BLOCKSTORAGE_H
#define BLOCKSTORAGE_H

#include "BlockInterface.h"

namespace toolkit {

class BlockWriterInterface {
public:
    virtual ~BlockWriterInterface() = default;

    virtual bool appendBlock(const BlockHeader& header, const uint8_t* payload, uint32_t payloadSize, bool flush_after = false) {
        if (!_writer) {
            _writer = createWriter();
        }
        int ret = _writer->write(&header, sizeof(header));
        if (ret < 0) {
            return false;
        }

        ret = _writer->write(payload, payloadSize);
        if (ret < 0) {
            return false;
        }

        if (flush_after) {
            _writer->flush();
        }

        _offset += sizeof(header) + payloadSize;
        return true;
    }

    virtual bool flush() {
        if (!_writer) return true;
        return _writer->flush() == 0;
    }

    virtual uint64_t position() const {
        return _offset;
    }

protected:
    virtual FileIOInterface::Writer createWriter() = 0;

    void resetStream() {
        _writer.reset();
        _offset = 0;
    }

private:
    FileIOInterface::Writer _writer;
    uint64_t _offset = 0;
};

class FileBlockWriter : public BlockWriterInterface {
public:
    void openFile(const std::string &path) {
        if (!path.empty()) {
            resetStream();
            _file_name = path;
            _file = std::make_shared<FileDisk>();
            _file->openFile(path, "wb");
        }
    }

    void closeFile() {
        flush();
        if (_file) {
            _file->closeFile();
            _file.reset();
        }
        resetStream();
    }

protected:
    FileIOInterface::Writer createWriter() override {
        return _file->createWriter();
    }

private:
    std::string _file_name;
    FileDisk::Ptr _file;
};

class BlockReaderInterface {
public:
    virtual ~BlockReaderInterface() = default;

    virtual bool readBlock(BlockHeader& header, std::vector<uint8_t>& payload, bool &eof) {
        eof = false;
        if (!_reader) {
            _reader = createReader();
        }
        int ret = _reader->read(&header, sizeof(header));
        if (ret < 0) {
            eof = true;
            return false;
        }

        payload.resize(header.payload_size);
        ret = _reader->read(payload.data(), header.payload_size);
        if (ret < 0) {
            eof = true;
            return false;
        }

        _offset += sizeof(header) + header.payload_size;
        return true;
    }

    virtual uint64_t position() const {
        return _offset;
    }

protected:
    virtual FileIOInterface::Reader createReader() = 0;

    void resetReader() {
        _reader.reset();
        _offset = 0;
    }

private:
    FileIOInterface::Reader _reader;
    uint64_t _offset = 0;
};

class FileBlockReader : public BlockReaderInterface {
public:
    void openFile(const std::string &path) {
        if (!path.empty()) {
            resetReader();
            _file_name = path;
            _file = std::make_shared<FileDisk>();
            _file->openFile(path, "rb");
        }
    }

    void closeFile() {
        if (_file) {
            _file->closeFile();
            _file.reset();
        }
        resetReader();
    }

protected:
    FileIOInterface::Reader createReader() override {
        return _file->createReader();
    }

private:
    std::string _file_name;
    FileDisk::Ptr _file;
};

} // namespace toolkit

#endif // BLOCKSTORAGE_H
