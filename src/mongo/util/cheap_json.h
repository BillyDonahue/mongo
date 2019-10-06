/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#include <array>
#include <ostream>

#include "mongo/base/string_data.h"
#include "mongo/bson/bsonelement.h"

namespace mongo {

class CheapJson {
public:
    class Sink;
    class Hex;
    class Val;

    explicit CheapJson(Sink& sink);

    Val doc();

private:
    Sink& _sink;
};

class CheapJson::Sink {
public:
    Sink& operator<<(StringData v) {
        doWrite(v);
        return *this;
    }

    Sink& operator<<(uint64_t v) {
        doWrite(v);
        return *this;
    }

private:
    virtual void doWrite(StringData v) = 0;
    virtual void doWrite(uint64_t v) = 0;
};

class CheapJson::Hex {
public:
    using Buf = std::array<char, 16>;
    static StringData toHex(uint64_t x, Buf& buf);
    static uint64_t fromHex(StringData s);
    explicit Hex(uintptr_t x) : _str(toHex(x, _buf)) {}
    StringData str() const {
        return _str;
    }

    friend Sink& operator<<(Sink& sink, const Hex& x) {
        return sink << x.str();
    }

private:
    Buf _buf;
    StringData _str;
};

class CheapJson::Val {
public:
    explicit Val(CheapJson* env) : Val(env, kDoc) {}
    ~Val();
    Val appendObj();
    Val appendArr();
    Val operator[](StringData k);
    void append(StringData v);
    void append(uint64_t v);
    void append(const BSONElement& be);

private:
    enum Kind { kDoc, kScalar, kObj, kArr, kKeyVal };

    Val(CheapJson* env, Kind k);
    void _copyBsonElementValue(const BSONElement& be);
    void _next();

    CheapJson* _env;
    Kind _kind;
    StringData _sep;
};

}  // namespace mongo
