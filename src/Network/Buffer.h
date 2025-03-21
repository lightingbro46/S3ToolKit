/*
 * Copyright (c) 2025 The S3ToolKit project authors. All Rights Reserved.
 *
 * This file is part of S3ToolKit(https://github.com/S3MediaKit/S3ToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef S3TOOLKIT_BUFFER_H
#define S3TOOLKIT_BUFFER_H

#include <cassert>
#include <memory>
#include <string>
#include <vector>
#include <type_traits>
#include <functional>
#include "Util/util.h"
#include "Util/ResourcePool.h"

namespace toolkit {

template <typename T> struct is_pointer : public std::false_type {};
template <typename T> struct is_pointer<std::shared_ptr<T> > : public std::true_type {};
template <typename T> struct is_pointer<std::shared_ptr<T const> > : public std::true_type {};
template <typename T> struct is_pointer<T*> : public std::true_type {};
template <typename T> struct is_pointer<const T*> : public std::true_type {};

//Cache base class
class Buffer : public noncopyable {
public:
    using Ptr = std::shared_ptr<Buffer>;

    Buffer() = default;
    virtual ~Buffer() = default;

    //Return data length
    virtual char *data() const = 0;
    virtual size_t size() const = 0;

    virtual std::string toString() const {
        return std::string(data(), size());
    }

    virtual size_t getCapacity() const {
        return size();
    }

private:
    //Object count statistics
    ObjectStatistic<Buffer> _statistic;
};

template <typename C>
class BufferOffset : public  Buffer {
public:
    using Ptr = std::shared_ptr<BufferOffset>;

    BufferOffset(C data, size_t offset = 0, size_t len = 0) : _data(std::move(data)) {
        setup(offset, len);
    }

    ~BufferOffset() override = default;

    char *data() const override {
        return const_cast<char *>(getPointer<C>(_data)->data()) + _offset;
    }

    size_t size() const override {
        return _size;
    }

    std::string toString() const override {
        return std::string(data(), size());
    }

private:
    void setup(size_t offset = 0, size_t size = 0) {
        auto max_size = getPointer<C>(_data)->size();
        assert(offset + size <= max_size);
        if (!size) {
            size = max_size - offset;
        }
        _size = size;
        _offset = offset;
    }

    template<typename T>
    static typename std::enable_if<::toolkit::is_pointer<T>::value, const T &>::type
    getPointer(const T &data) {
        return data;
    }

    template<typename T>
    static typename std::enable_if<!::toolkit::is_pointer<T>::value, const T *>::type
    getPointer(const T &data) {
        return &data;
    }

private:
    C _data;
    size_t _size;
    size_t _offset;
};

using BufferString = BufferOffset<std::string>;

//Pointer-style cache object,
class BufferRaw : public Buffer {
public:
    using Ptr = std::shared_ptr<BufferRaw>;

    static Ptr create();

    ~BufferRaw() override {
        if (_data) {
            delete[] _data;
        }
    }

    //When writing data, please ensure that the memory does not overflow
    char *data() const override {
        return _data;
    }

    //Effective data size
    size_t size() const override {
        return _size;
    }

    //Allocated memory size
    void setCapacity(size_t capacity) {
        if (_data) {
            do {
                if (capacity > _capacity) {
                    //If the requested memory is greater than the current memory, reallocate
                    break;
                }

                if (_capacity < 2 * 1024) {
                    //Less than 2K, do not repeatedly allocate memory, reuse directly
                    return;
                }

                if (2 * capacity > _capacity) {
                    //If the requested memory is greater than half of the current memory, also reuse
                    return;
                }
            } while (false);

            delete[] _data;
        }
        _data = new char[capacity];
        _capacity = capacity;
    }

    //Set valid data size
    virtual void setSize(size_t size) {
        if (size > _capacity) {
            throw std::invalid_argument("Buffer::setSize out of range");
        }
        _size = size;
    }

    //Assign data
    void assign(const char *data, size_t size = 0) {
        if (size <= 0) {
            size = strlen(data);
        }
        setCapacity(size + 1);
        memcpy(_data, data, size);
        _data[size] = '\0';
        setSize(size);
    }

    size_t getCapacity() const override {
        return _capacity;
    }

protected:
    friend class ResourcePool_l<BufferRaw>;

    BufferRaw(size_t capacity = 0) {
        if (capacity) {
            setCapacity(capacity);
        }
    }

    BufferRaw(const char *data, size_t size = 0) {
        assign(data, size);
    }

private:
    size_t _size = 0;
    size_t _capacity = 0;
    char *_data = nullptr;
    //Object count statistics
    ObjectStatistic<BufferRaw> _statistic;
};

class BufferLikeString : public Buffer {
public:
    ~BufferLikeString() override = default;

    BufferLikeString() {
        _erase_head = 0;
        _erase_tail = 0;
    }

    BufferLikeString(std::string str) {
        _str = std::move(str);
        _erase_head = 0;
        _erase_tail = 0;
    }

    BufferLikeString &operator=(std::string str) {
        _str = std::move(str);
        _erase_head = 0;
        _erase_tail = 0;
        return *this;
    }

    BufferLikeString(const char *str) {
        _str = str;
        _erase_head = 0;
        _erase_tail = 0;
    }

    BufferLikeString &operator=(const char *str) {
        _str = str;
        _erase_head = 0;
        _erase_tail = 0;
        return *this;
    }

    BufferLikeString(BufferLikeString &&that) {
        _str = std::move(that._str);
        _erase_head = that._erase_head;
        _erase_tail = that._erase_tail;
        that._erase_head = 0;
        that._erase_tail = 0;
    }

    BufferLikeString &operator=(BufferLikeString &&that) {
        _str = std::move(that._str);
        _erase_head = that._erase_head;
        _erase_tail = that._erase_tail;
        that._erase_head = 0;
        that._erase_tail = 0;
        return *this;
    }

    BufferLikeString(const BufferLikeString &that) {
        _str = that._str;
        _erase_head = that._erase_head;
        _erase_tail = that._erase_tail;
    }

    BufferLikeString &operator=(const BufferLikeString &that) {
        _str = that._str;
        _erase_head = that._erase_head;
        _erase_tail = that._erase_tail;
        return *this;
    }

    char *data() const override {
        return (char *) _str.data() + _erase_head;
    }

    size_t size() const override {
        return _str.size() - _erase_tail - _erase_head;
    }

    BufferLikeString &erase(size_t pos = 0, size_t n = std::string::npos) {
        if (pos == 0) {
            //Remove data from the front
            if (n != std::string::npos) {
                //Remove part
                if (n > size()) {
                    //Removed too much data
                    throw std::out_of_range("BufferLikeString::erase out_of_range in head");
                }
                //Set starting offset
                _erase_head += n;
                data()[size()] = '\0';
                return *this;
            }
            //Remove all data
            _erase_head = 0;
            _erase_tail = _str.size();
            data()[0] = '\0';
            return *this;
        }

        if (n == std::string::npos || pos + n >= size()) {
            //Remove all data from the end
            if (pos >= size()) {
                //Removed too much data
                throw std::out_of_range("BufferLikeString::erase out_of_range in tail");
            }
            _erase_tail += size() - pos;
            data()[size()] = '\0';
            return *this;
        }

        //Remove the middle
        if (pos + n > size()) {
            //Exceeds the length limit
            throw std::out_of_range("BufferLikeString::erase out_of_range in middle");
        }
        _str.erase(_erase_head + pos, n);
        return *this;
    }

    BufferLikeString &append(const BufferLikeString &str) {
        return append(str.data(), str.size());
    }

    BufferLikeString &append(const std::string &str) {
        return append(str.data(), str.size());
    }

    BufferLikeString &append(const char *data) {
        return append(data, strlen(data));
    }

    BufferLikeString &append(const char *data, size_t len) {
        if (len <= 0) {
            return *this;
        }
        if (_erase_head > _str.capacity() / 2) {
            moveData();
        }
        if (_erase_tail == 0) {
            _str.append(data, len);
            return *this;
        }
        _str.insert(_erase_head + size(), data, len);
        return *this;
    }

    void push_back(char c) {
        if (_erase_tail == 0) {
            _str.push_back(c);
            return;
        }
        data()[size()] = c;
        --_erase_tail;
        data()[size()] = '\0';
    }

    BufferLikeString &insert(size_t pos, const char *s, size_t n) {
        _str.insert(_erase_head + pos, s, n);
        return *this;
    }

    BufferLikeString &assign(const char *data) {
        return assign(data, strlen(data));
    }

    BufferLikeString &assign(const char *data, size_t len) {
        if (len <= 0) {
            return *this;
        }
        if (data >= _str.data() && data < _str.data() + _str.size()) {
            _erase_head = data - _str.data();
            if (data + len > _str.data() + _str.size()) {
                throw std::out_of_range("BufferLikeString::assign out_of_range");
            }
            _erase_tail = _str.data() + _str.size() - (data + len);
            return *this;
        }
        _str.assign(data, len);
        _erase_head = 0;
        _erase_tail = 0;
        return *this;
    }

    void clear() {
        _erase_head = 0;
        _erase_tail = 0;
        _str.clear();
    }

    char &operator[](size_t pos) {
        if (pos >= size()) {
            throw std::out_of_range("BufferLikeString::operator[] out_of_range");
        }
        return data()[pos];
    }

    const char &operator[](size_t pos) const {
        return (*const_cast<BufferLikeString *>(this))[pos];
    }

    size_t capacity() const {
        return _str.capacity();
    }

    void reserve(size_t size) {
        _str.reserve(size);
    }

    void resize(size_t size, char c = '\0') {
        _str.resize(size, c);
        _erase_head = 0;
        _erase_tail = 0;
    }

    bool empty() const {
        return size() <= 0;
    }

    std::string substr(size_t pos, size_t n = std::string::npos) const {
        if (n == std::string::npos) {
            //Get all at the end
            if (pos >= size()) {
                throw std::out_of_range("BufferLikeString::substr out_of_range");
            }
            return _str.substr(_erase_head + pos, size() - pos);
        }

        //Get part
        if (pos + n > size()) {
            throw std::out_of_range("BufferLikeString::substr out_of_range");
        }
        return _str.substr(_erase_head + pos, n);
    }

private:
    void moveData() {
        if (_erase_head) {
            _str.erase(0, _erase_head);
            _erase_head = 0;
        }
    }

private:
    size_t _erase_head;
    size_t _erase_tail;
    std::string _str;
    //Object count statistics
    ObjectStatistic<BufferLikeString> _statistic;
};

}//namespace toolkit
#endif //S3TOOLKIT_BUFFER_H
