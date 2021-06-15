#!/usr/bin/env python2

import subprocess
import sys
import os

from itertools import combinations

iters = 1000000


def __get_values(filename):
    tok = open(filename, 'r').readlines()
    tok = map(lambda x: int(x), tok)
    return min(tok)

max_offset = 0
files=[f for f in os.listdir(sys.argv[1]) if os.path.isfile(os.path.join(sys.argv[1], f))]

cores = [x for x in range(int(sys.argv[2]))]

for i in range(len(cores)):
    for j in range(len(cores)):
        if i == j:
            print ("0 "),
            continue
        print("%d " % __get_values("%s/%d-%d.txt" % (sys.argv[1], i, j))),
    print("")

