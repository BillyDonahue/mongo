#!/usr/bin/env python3
#
# Copyright (C) 2021-present MongoDB, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the Server Side Public License, version 1,
# as published by MongoDB, Inc.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# Server Side Public License for more details.
#
# You should have received a copy of the Server Side Public License
# along with this program. If not, see
# <http://www.mongodb.com/licensing/server-side-public-license>.
#
# As a special exception, the copyright holders give permission to link the
# code of portions of this program with the OpenSSL library under certain
# conditions as described in each individual source file and distribute
# linked combinations including the program with the OpenSSL library. You
# must comply with the Server Side Public License in all respects for
# all of the code used other than as permitted herein. If you modify file(s)
# with this exception, you may extend this exception to your version of the
# file(s), but you are not obligated to do so. If you do not wish to do so,
# delete this exception statement from your version. If you delete this
# exception statement from all source files in the program, then also delete
# it in the license file.
"""Feed a YAML document to a Cheetah template to generate a source file."""

from Cheetah.Template import Template
import argparse
import sys
import yaml

def main():
    """Generate a file by passing a YAML object to a Cheetah template."""
    parser = argparse.ArgumentParser(
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=__doc__)
    parser.add_argument('yaml_file', help='YAML input file')
    parser.add_argument('template_file', help='Cheetah template file')
    parser.add_argument('output_file')

    opts = parser.parse_args()

    with open(opts.yaml_file, 'r') as in_file:
        data = yaml.safe_load(in_file)

    template = Template.compile(
        file=opts.template_file,
        compilerSettings=dict(directiveStartToken="//#", directiveEndToken="//#",
                              commentStartToken="//##"), baseclass=dict, useCache=False)
    text = str(template(**data))

    with open(opts.output_file, 'w') as out_file:
        out_file.write(text)


if __name__ == '__main__':
    main()
