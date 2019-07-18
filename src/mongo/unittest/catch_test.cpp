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

#include <catch2/catch.hpp>

unsigned int Factorial( unsigned int number ) {
    return number <= 1 ? number : Factorial(number-1)*number;
}

CATCH_TEST_CASE("Factorials are computed", "[factorial]") {
    CATCH_REQUIRE( Factorial(1) == 1 );
    CATCH_REQUIRE( Factorial(2) == 2 );
    CATCH_REQUIRE( Factorial(3) == 6 );
    CATCH_REQUIRE( Factorial(10) == 3628800 );
}

CATCH_TEST_CASE( "vectors can be sized and resized", "[vector]" ) {
    std::vector<int> v( 5 );
    CATCH_REQUIRE( v.size() == 5 );
    CATCH_REQUIRE( v.capacity() >= 5 );
    CATCH_SECTION( "resizing bigger changes size and capacity" ) {
        v.resize( 10 );
        CATCH_REQUIRE( v.size() == 10 );
        CATCH_REQUIRE( v.capacity() >= 10 );
    }
    CATCH_SECTION( "resizing smaller changes size but not capacity" ) {
        v.resize( 0 );
        CATCH_REQUIRE( v.size() == 0 );
        CATCH_REQUIRE( v.capacity() >= 5 );
    }
    CATCH_SECTION( "reserving bigger changes capacity but not size" ) {
        v.reserve( 10 );
        CATCH_REQUIRE( v.size() == 5 );
        CATCH_REQUIRE( v.capacity() >= 10 );
    }
    CATCH_SECTION( "reserving smaller does not change size or capacity" ) {
        v.reserve( 0 );
        CATCH_REQUIRE( v.size() == 5 );
        CATCH_REQUIRE( v.capacity() >= 5 );
    }
}
