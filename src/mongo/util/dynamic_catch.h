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

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mongo {

/**
 * Provides a mechanism for responding to an active exception with
 * a dynamic registry of handlers for exception types.
 *
 * A handler is selected by probing the handlers in LIFO order. The rightmost
 * handler that responds affirmatively is given the exception via its callback,
 * along with any extra parameters to `doCatch(...)`. The types of these
 * extra parameters are the `Args...` of `DynamicCatch<Args...>`.
 *
 *     DynamicCatch<A0, A1> dc;
 *     dc.addCatch<T0>(cb0);
 *     dc.addCatch<T1>(cb1);
 *     dc.addCatch<T2>(cb2);
 *     dc.doCatch(a0, a1);
 *
 * Is roughly equivalent to:
 *
 *     try { throw; }
 *     catch (const T2& ex) { cb2(ex, a0, a1); }
 *     catch (const T1& ex) { cb1(ex, a0, a1); }
 *     catch (const T0& ex) { cb0(ex, a0, a1); }
 */
template <typename... Args>
class DynamicCatch {
public:
    /**
     * Add a probe for exception type T. If an exception `const T& ex` is
     * caught by `doCatch(...args)`, then `f(ex, ...args)` will be invoked.
     */
    template <typename T, typename F>
    void addCatch(F f) {
        _handlers.push_back(std::make_unique<Handler<T, F>>(std::move(f)));
    }

    /**
     * May only be called while an exception is active. Visits each handler
     * starting from the back, until one catches the exception. If no handler
     * catches the active exception, it will be rethrown by this function.
     */
    void doCatch(Args... args) {
        for (auto iter = _handlers.rbegin(); iter != _handlers.rend(); ++iter)
            if ((*iter)->tryRun(args...))
                return;
        throw;  // uncaught
    }

private:
    /** A type-erased exception handler. */
    struct AbstractHandler {
        virtual ~AbstractHandler() = default;

        /**
         * Handlers must try to rethrow and catch the active exception. If it
         * is caught, take an action with `Args...` and return true.
         */
        virtual bool tryRun(Args...) const = 0;
    };

    /**
     * Handler that invokes `f(ex, args...)` if an exception `ex` of type `Ex`
     * is active.
     */
    template <typename Ex, typename F>
    struct Handler : AbstractHandler {
        Handler(F f) : f(std::move(f)) {}

        bool tryRun(Args... args) const override {
            try {
                throw;
            } catch (const Ex& ex) {
                f(ex, args...);
                return true;
            } catch (...) {
                return false;
            }
        }

        F f;
    };

    std::vector<std::unique_ptr<AbstractHandler>> _handlers;
};

}  // namespace mongo
