#!/usr/bin/env python3

import re
import os
import sys

def proc(results):
    points = {}
    for line in results.split("\n"):
        #"BM_arrayBuilder/1                37 ns         37 ns   19224856   286.644MB/s"
        #print(line)
        rem = re.match('BM_arrayBuilder/([0-9]*)\s*(\d*) ns\s*(\d*) ns.*\s([0-9.]*)MB/s', line)
        if rem:
            n = int(rem[1])
            t = float(rem[3])
            points[n] = t / n
    return points

s0 = open(sys.argv[1], "r").read()
s1 = open(sys.argv[2], "r").read()
d0 = proc(s0)
d1 = proc(s1)

print('{:<6s} : {:<12s} : {:<12s} : {:<8s}'.format('N', 'before(ns)', 'after(ns)', '%chg'))
for k in d0.keys():
    chg = (d1[k] - d0[k]) / d0[k]
    print(f'{k:6d} : {d0[k]:12.3f} : {d1[k]:12.3f} : {100*chg:+8.03f}')
