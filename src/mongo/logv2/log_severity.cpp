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

#include "mongo/logv2/log_severity.h"

namespace mongo::logv2 {

StringData LogSeverity::toStringData() const {
    if ((*this == LogSeverity::Log()) || (*this == LogSeverity::Info()))
        return "info"_sd;
    if (_severity > 0)
        return "debug"_sd;
    if (*this == LogSeverity::Warning())
        return "warning"_sd;
    if (*this == LogSeverity::Error())
        return "ERROR"_sd;
    if (*this == LogSeverity::Severe())
        return "SEVERE"_sd;
    return "UNKNOWN"_sd;
}

StringData LogSeverity::toStringDataCompact() const {
    if ((*this == LogSeverity::Log()) || (*this == LogSeverity::Info()))
        return "I"_sd;
    if ((_severity > 0) && (_severity <= kMaxDebugLevel)) {
        static constexpr StringData arr[LogSeverity::kMaxDebugLevel] = {
            "D1"_sd,
            "D2"_sd,
            "D3"_sd,
            "D4"_sd,
            "D5"_sd,
        };
        return arr[_severity - 1];
    }
    if (*this == LogSeverity::Warning())
        return "W"_sd;
    if (*this == LogSeverity::Error())
        return "E"_sd;
    if (*this == LogSeverity::Severe())
        return "F"_sd;  // "F" for "Fatal", as "S" might be confused with "Success".
    return "U"_sd;
}

}  // namespace mongo::logv2
