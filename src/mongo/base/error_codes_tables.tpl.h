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

#define ERROR_CODES_EMPTY
#define ERROR_CODES_COMMA ,
#define ERROR_CODES_UNPACK_1(...) __VA_ARGS__
#define ERROR_CODES_UNPACK(...) ERROR_CODES_UNPACK_1 __VA_ARGS__

#define ERROR_CODES_FOR_EACH_CODE(elem,comma) \
//#set $sep = '      '
//#for $ec in $codes
    ${sep}elem($ec.name, $ec.code) \
//#set $sep = 'comma '
//#end for
    /**/

#define ERROR_CODES_FOR_EACH_CATEGORY(elem,comma) \
//#set $sep = '      '
//#for $cat in $categories
    ${sep}elem(${cat.name}) \
//#set $sep = 'comma '
//#end for
    /**/

#define ERROR_CODES_FOR_EACH_EXTRA_INFO(onExtraClass, onExtraClassNs) \
//#for $ec in $codes:
//#if $ec.extra
//#if $ec.extra_ns
    onExtraClassNs($ec.name, $ec.extra_class, $ec.extra_ns) \
//#else
    onExtraClass($ec.name, $ec.extra_class) \
//#end if
//#end if
//#end for
    /**/

#define ERROR_CODES_FOR_EACH_CODE_THEN_CATEGORY(onCode, onCodeCategory, catSep) \
//#for $ec in $codes:
//#if len($ec.categories) == 0:
    onCode($ec.name, ()) \
//#else
    onCode($ec.name, ( \
//#set $sep = '       '
//#for $i, $cat in enumerate($ec.categories)
        ${sep}onCodeCategory($ec.name, $cat) \
//#set $sep = 'catSep '
//#end for
        )) \
//#end if
//#end for
    /**/
