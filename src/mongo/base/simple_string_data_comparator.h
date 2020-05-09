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

#include "mongo/base/string_data_comparator.h"

namespace mongo {

/**
 * Compares and hashes strings using simple binary comparisons.
 */
class SimpleStringDataComparator final : public StringDataComparator {
public:
    // Global comparator for performing simple binary string comparisons. String comparisons that
    // require database logic, such as collations, must instantiate their own comparator.
    static const SimpleStringDataComparator& instance();

    constexpr SimpleStringDataComparator() = default;

    int compare(StringData left, StringData right) const override {
        return left.compare(right);
    }

    bool equal(StringData left, StringData right) const override {
        return _equal(left, right);
    }

    void hash_combine(size_t& seed, StringData s) const override {
        seed = _hash(seed, s);
    }

    struct Hasher {
        size_t operator()(StringData s) const {
            return _hash(0, s);
        }
    };

    struct EqualTo {
        bool operator()(StringData a, StringData b) const {
            return _equal(a, b);
        }
    };

private:
    static size_t _hash(size_t seed, StringData s);
    static bool _equal(StringData a, StringData b) { return a == b; }
};

}  // namespace mongo
