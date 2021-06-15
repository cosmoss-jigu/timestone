#!/usr/bin/env python3

import os
import sys


offset_table = []


def get_freq(a):
    v = {}
    for i in a:
        if i in v:
            v[i] += 1
        else:
            v.update({i: 1})
    return v


def get_freq_table(filename, outfilename):
    a = []
    with open(filename, "r") as f:
        a = f.readlines()
    for i in range(0, len(a)):
        a[i] = int(a[i])
    freq_table = get_freq(a)
    with open(outfilename, "w") as f:
        for k, v in sorted(freq_table.items()):
            f.write("%s, %s\n" % (str(k), str(v)))


def main():
    if len(sys.argv) < 3:
        print("%s filename output" % sys.argv[0])
        return -1

    get_freq_table(sys.argv[1], sys.argv[2])

if __name__ == '__main__':
    main()
