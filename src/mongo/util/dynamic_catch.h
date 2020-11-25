/**
 *    Copyright (C) 2020-present MongoDB, Inc.
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

#include <exception>
#include <functional>
#include <iosfwd>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <typeinfo>
#include <vector>

#include <fmt/format.h>

#include "mongo/util/assert_util.h"

namespace mongo {

/**
 * Provides a mechanism for diagnosing an active exception.
 *
 * The active exception is rethrown at the deepest level of a
 * recursively nested `try` block.
 *
 *     DynamicCatch dc;
 *     dc.addCatch<T0>(cb0);
 *     dc.addCatch<T1>(cb1);
 *     dc.addCatch<T2>(cb2);
 *     dc.doCatch(os);
 *
 * Is roughly equivalent to:
 *
 *     try {
 *       try {
 *         try {
 *           throw;
 *         } catch (const T2& ex) { cb2(ex, os); }
 *       } catch (const T1& ex) { cb1(ex, os); }
 *     } catch (const T0& ex) { cb0(ex, os); }
 */
class DynamicCatch {
public:
    /**
     * Must be called while an exception is active. Tries to print a
     * description of the active exception to `os` by consulting the probe
     * chain. If nothing in the chain catches the active exception, it will
     * appear to be rethrown by this function.
     */
    void doCatch(std::ostream& os) {
        _pos = 0;
        _os = &os;
        _nextHandler();
    }


    /**
     * Add a probe for for exception type T to the end of the chain. If an
     * exception `const T& ex` is caught by `doCatch(os)`, then `f(ex,
     * os)` will be invoked.
     */
    template <typename T, typename F>
    void addCatch(F&& f) {
        _chain.push_back([f = std::move(f)](DynamicCatch& dc) { dc._typedHandler<T>(f); });
    }

private:
    /**
     * Called inside a probe callback. Creates a try/catch to catch `const T&`,
     * and invokes the next probe in in the probe chain. This sets up a call
     * stack of catch blocks in the order of the probe objects. When the end of
     * the chain is reached, we rethrow. `describe` is a callable that
     * can describe a `const T&` into a `std::ostream`.
     * This function ultimately rethrows the active exception, so if that
     * exception isn't caught by any subsequent probes in the probe chain,
     * and it isn't a `const T&`, it will escape this call.
     */
    template <typename T, typename F>
    void _typedHandler(F&& describe) {
        try {
            _nextHandler();
        } catch (const T& ex) {
            std::invoke(std::forward<F>(describe), ex, *_os);
            *_os << "\nActual exception type: " << demangleName(typeid(ex)) << "\n";
        }
    }

    void _nextHandler() {
        if (_pos == _chain.size())
            throw;
        _chain[_pos++](*this);
    }

    std::vector<std::function<void(DynamicCatch&)>> _chain;
    size_t _pos = 0;
    std::ostream* _os = nullptr;
};

}  // namespace mongo
