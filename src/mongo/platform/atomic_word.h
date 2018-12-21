
/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <atomic>
#include <cstring>
#include <type_traits>

#include "mongo/base/static_assert.h"
#include "mongo/stdx/type_traits.h"

namespace mongo {

namespace atomic_word_detail {

enum class Category { kInvalid, kIntegral, kNonintegral };

template <typename T>
constexpr Category getCategory() {
    if (std::is_integral<T>())
        return Category::kIntegral;
    if (sizeof(T) <= sizeof(uint64_t) && std::is_trivially_copyable<T>())
        return Category::kNonintegral;
    return Category::kInvalid;
}

template <typename T, Category = getCategory<T>()>
class AtomicWordImpl;

/**
 * Implementation of the AtomicWord interface in terms of the C++11 Atomics.
 */
template <typename T>
class AtomicWordImpl<T, Category::kIntegral> {
public:
    /**
     * Underlying value type.
     */
    using WordType = T;

    /**
     * Construct a new word with the given initial value.
     */
    explicit constexpr AtomicWordImpl(WordType value = WordType(0)) : _value(value) {}

    /**
     * Gets the current value of this AtomicWord.
     *
     * Has acquire and release semantics.
     */
    WordType load() const {
        return _value.load();
    }

    /**
     * Gets the current value of this AtomicWord.
     *
     * Has relaxed semantics.
     */
    WordType loadRelaxed() const {
        return _value.load(std::memory_order_relaxed);
    }

    /**
     * Sets the value of this AtomicWord to "newValue".
     *
     * Has acquire and release semantics.
     */
    void store(WordType newValue) {
        return _value.store(newValue);
    }

    /**
     * Atomically swaps the current value of this with "newValue".
     *
     * Returns the old value.
     *
     * Has acquire and release semantics.
     */
    WordType swap(WordType newValue) {
        return _value.exchange(newValue);
    }

    /**
     * Atomic compare and swap.
     *
     * If this value equals "expected", sets this to "newValue".
     * Always returns the original of this.
     *
     * Has acquire and release semantics.
     */
    WordType compareAndSwap(WordType expected, WordType newValue) {
        // NOTE: Subtle: compare_exchange mutates its first argument.
        _value.compare_exchange_strong(expected, newValue);
        return expected;
    }

    /**
     * Get the current value of this, add "increment" and store it, atomically.
     *
     * Returns the value of this before incrementing.
     *
     * Has acquire and release semantics.
     */
    WordType fetchAndAdd(WordType increment) {
        return _value.fetch_add(increment);
    }

    /**
     * Like "fetchAndAdd", but with relaxed memory order. Appropriate where relative
     * order of operations doesn't matter. A stat counter, for example.
     */
    WordType fetchAndAddRelaxed(WordType increment) {
        return _value.fetch_add(increment, std::memory_order_relaxed);
    }

    /**
     * Get the current value of this, subtract "decrement" and store it, atomically.
     *
     * Returns the value of this before decrementing.
     *
     * Has acquire and release semantics.
     */
    WordType fetchAndSubtract(WordType decrement) {
        return _value.fetch_sub(decrement);
    }

    /**
     * Get the current value of this, add "increment" and store it, atomically.
     *
     * Returns the value of this after incrementing.
     *
     * Has acquire and release semantics.
     */
    WordType addAndFetch(WordType increment) {
        return fetchAndAdd(increment) + increment;
    }

    /**
     * Get the current value of this, subtract "decrement" and store it, atomically.
     *
     * Returns the value of this after decrementing.
     *
     * Has acquire and release semantics.
     */
    WordType subtractAndFetch(WordType decrement) {
        return fetchAndSubtract(decrement) - decrement;
    }

private:
    std::atomic<WordType> _value;  // NOLINT
};

/**
 * Implementation of the AtomicWord interface for non-integral types that are trivially copyable and
 * fit in 8 bytes.  For that implementation we flow reads and writes through memcpy'ing bytes in and
 * out of a uint64_t, then relying on std::atomic<uint64_t>.
 */
template <typename T>
class AtomicWordImpl<T, Category::kNonintegral> {
    using StorageType = uint64_t;

public:
    /**
     * Underlying value type.
     */
    using WordType = T;

    /**
     * Construct a new word with the given initial value.
     */
    explicit AtomicWordImpl(WordType value = WordType{}) {
        store(value);
    }

    // Used in invoking a zero'd out non-integral atomic word
    struct ZeroInitTag {};
    /**
     * Construct a new word with zero'd out bytes.  Useful if you need a constexpr AtomicWord of
     * non-integral type.
     */
    constexpr explicit AtomicWordImpl(ZeroInitTag) : _storage(0) {}

    /**
     * Gets the current value of this AtomicWord.
     *
     * Has acquire and release semantics.
     */
    WordType load() const {
        return _fromStorage(_storage.load());
    }

    /**
     * Gets the current value of this AtomicWord.
     *
     * Has relaxed semantics.
     */
    WordType loadRelaxed() const {
        return _fromStorage(_storage.load(std::memory_order_relaxed));
    }

    /**
     * Sets the value of this AtomicWord to "newValue".
     *
     * Has acquire and release semantics.
     */
    void store(WordType newValue) {
        _storage.store(_toStorage(newValue));
    }

    /**
     * Atomically swaps the current value of this with "newValue".
     *
     * Returns the old value.
     *
     * Has acquire and release semantics.
     */
    WordType swap(WordType newValue) {
        return _fromStorage(_storage.exchange(_toStorage(newValue)));
    }

    /**
     * Atomic compare and swap.
     *
     * If this value equals "expected", sets this to "newValue".
     * Always returns the original of this.
     *
     * Has acquire and release semantics.
     */
    WordType compareAndSwap(WordType expected, WordType newValue) {
        // NOTE: Subtle: compare_exchange mutates its first argument.
        auto v = _toStorage(expected);
        _storage.compare_exchange_strong(v, _toStorage(newValue));
        return _fromStorage(v);
    }

private:
    template <typename U>
    static char* _asBytes(U* p) {
        return reinterpret_cast<char*>(p);
    }
    template <typename U>
    static const char* _asBytes(const U* p) {
        return reinterpret_cast<const char*>(p);
    }

    static WordType _fromStorage(StorageType storage) noexcept {
        WordType v;
        memcpy(_asBytes(&v), _asBytes(&storage), sizeof(WordType));
        return v;
    }

    static StorageType _toStorage(WordType word) noexcept {
        StorageType v = 0;
        memcpy(_asBytes(&v), _asBytes(&word), sizeof(WordType));
        return v;
    }

    std::atomic<StorageType> _storage;  // NOLINT
};

}  // namespace atomic_word_detail

/**
 * Instantiations of AtomicWord must be Integral, or Trivally Copyable and 8 bytes or less.
 */
template <typename T>
class AtomicWord : public atomic_word_detail::AtomicWordImpl<T> {
    using Base_ = atomic_word_detail::AtomicWordImpl<T>;
    static_assert(!std::is_integral<T>() || sizeof(Base_) == sizeof(T), "");
    static_assert(!std::is_integral<T>() || std::is_standard_layout<T>(), "");

public:
    using Base_::Base_;
};

using AtomicUInt32 = AtomicWord<unsigned>;
using AtomicUInt64 = AtomicWord<unsigned long long>;
using AtomicInt32 = AtomicWord<int>;
using AtomicInt64 = AtomicWord<long long>;
using AtomicBool = AtomicWord<bool>;

}  // namespace mongo
