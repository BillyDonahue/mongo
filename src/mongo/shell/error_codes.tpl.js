
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

var {ErrorCodes, ErrorCodeStrings} = (function() {
    //## Using minimal Cheetah directives to generate two tables and no code.
    const ErrorCodesObject = {
        //#for $ec in $codes
        '$ec.name': $ec.code,
        //#end for
    };

    const CategoryCodesObject = {
        //#for $cat in $categories
        '${cat.name}': [
            //#for $code in $cat.codes
            '$code',
            //#end for
        ],
        //#end for
    };
    //## Pure JavaScript after here.

    const handler = {
        get: function(obj, prop) {
            if (typeof prop !== "symbol" && prop in obj === false && prop in Object === false) {
                throw new Error('Unknown Error Code: ' + prop.toString());
            }

            return obj[prop];
        }
    };

    var reverseMapping = function(obj) {
        var ret = {};
        for (var key in obj) {
            ret[obj[key]] = key;
        }
        return ret;
    };

    var ecProxy = new Proxy(ErrorCodesObject, handler);
    var ecStringsProxy = new Proxy(reverseMapping(ErrorCodesObject), handler);

    const fixErrArg  = function(err) {
        'use strict';

        if (typeof err === 'string') {
            return err;
        }
        if (typeof err === 'number') {
            if (Object.prototype.hasOwnProperty.call(ecStringsProxy, err)) {
                return ecStringsProxy[err];
            }
            return null;
        }
        return err;
    };

    for (var cat in CategoryCodesObject) {
        const catCodes = new Set(CategoryCodesObject[cat]);
        ecProxy['is' + cat] = function(err) {
            'use strict';
            const error = fixErrArg(err);
            return (error !== null) && catCodes.has(error);
        };
    }

    return {
        ErrorCodes: ecProxy,
        ErrorCodeStrings: ecStringsProxy,
    };
})();
