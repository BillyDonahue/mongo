#!/usr/bin/env python3

import re
import sys

class FilterMakeOutput(object):
    def __init__(self, filename):
        self = {}
        self['filename'] = filename
        f = open(filename, 'rt')
        self['file'] = f
        print("init filename: '{}'".format(self['filename']))
        self['data'] = list()

        for line in f:
            self['data'].append(line)

        n = 1
        for d in self['data']:
            print(f'  data[{n:3d}]: {d}', end='')
            n += 1

def main():
    print("sys.argv = {}".format(len(sys.argv)))
    assert len(sys.argv) >= 2
    filter = FilterMakeOutput(sys.argv[1])

if __name__ == '__main__':
    main()
