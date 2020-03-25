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

#include "mongo/platform/basic.h"

#include "mongo/logv2/log_component_settings.h"

#include "mongo/util/assert_util.h"

namespace mongo::logv2 {

namespace {
constexpr auto intToComponent(int i) {
    return LogComponent(LogComponent::Value(i));
}
constexpr auto componentToInt(LogComponent c) {
    return int(LogComponent::Value(c));
}

void dassertValid(LogComponent component) {
    dassert(int(component) >= 0 && int(component) < LogComponent::kNumLogComponents);
}

}  // namespace


LogComponentSettings::LogComponentSettings() {
    static constexpr int severity = LogSeverity::Log().toInt();
    for (int i = 0; i < int(LogComponent::kNumLogComponents); ++i) {
        _minimumLoggedSeverity[i].store(severity);
        _hasMinimumLoggedSeverity[i].store(false);
    }
    _hasMinimumLoggedSeverity[componentToInt(LogComponent::kDefault)].store(true);
}

LogComponentSettings::~LogComponentSettings() {}

bool LogComponentSettings::hasMinimumLogSeverity(LogComponent component) const {
    dassertValid(component);
    return _hasMinimumLoggedSeverity[component].load();
}

LogSeverity LogComponentSettings::getMinimumLogSeverity(LogComponent component) const {
    dassertValid(component);
    return LogSeverity::cast(_minimumLoggedSeverity[component].load());
}

void LogComponentSettings::setMinimumLoggedSeverity(LogComponent component, LogSeverity severity) {
    dassertValid(component);
    stdx::lock_guard<Latch> lk(_mtx);
    _setMinimumLoggedSeverityInLock(component, severity);
}

void LogComponentSettings::_setMinimumLoggedSeverityInLock(LogComponent component,
                                                           LogSeverity severity) {
    _minimumLoggedSeverity[component].store(severity.toInt());
    _hasMinimumLoggedSeverity[component].store(true);

    // Unconfigured components inherit log severity from the nearest configured ancestor.
    for (int i = 0; i < int(LogComponent::kNumLogComponents); ++i) {
        // If `i` does not have a configured severity, find an ancestor `a` that does.
        for (int a = i;; a = componentToInt(intToComponent(a).parent())) {
            // An ancestor a < i has already been propagated to.
            if (a < i || _hasMinimumLoggedSeverity[a].load()) {
                if (a != i)  // Don't self-assign.
                    _minimumLoggedSeverity[i].store(_minimumLoggedSeverity[a].load());
                break;
            }
        }
    }

    if (kDebugBuild) {
        // Verify that either an element has an individual log severity set or that its
        // value is equal to its parent's (i.e. either the value is set or inherited).
        for (int i = 0; i < int(LogComponent::kNumLogComponents); ++i) {
            LogComponent parent = intToComponent(i).parent();
            invariant(_hasMinimumLoggedSeverity[i].load() ||
                      _minimumLoggedSeverity[i].load() == _minimumLoggedSeverity[parent].load());
        }
    }
}

void LogComponentSettings::clearMinimumLoggedSeverity(LogComponent component) {
    dassertValid(component);

    stdx::lock_guard<Latch> lk(_mtx);

    // LogComponent::kDefault must always be configured.
    if (component == LogComponent::kDefault) {
        _setMinimumLoggedSeverityInLock(component, LogSeverity::Log());
        return;
    }

    // Set unconfigured severity level to match LogComponent::kDefault.
    _setMinimumLoggedSeverityInLock(component, getMinimumLogSeverity(component.parent()));
    _hasMinimumLoggedSeverity[component].store(false);
}

bool LogComponentSettings::shouldLog(LogComponent component, LogSeverity severity) const {
    dassertValid(component);
    return severity >= LogSeverity::cast(_minimumLoggedSeverity[component].loadRelaxed());
}

}  // namespace mongo::logv2
