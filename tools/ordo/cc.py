#!/usr/bin/env python3

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


def process_file(filename):
    freq = {}
    a = []
    median = 0
    with open(filename, "r") as f:
        a = f.readlines()
    for i in range(0, len(a)):
        a[i] = int(a[i])
    a.sort()
    s = int(len(a))
    if s % 2 == 0:
        median = (a[int(s/2) -1] + a[int(s/2)]) / 2
    else:
        median = a[int(s/2)-1]
    print("min: %d max: %d median: %d" % (a[0], a[int(s)-1], median))


def main():
    if len(sys.argv) < 2:
        print("%s filename" % sys.argv[0])
        return -1

    process_file(sys.argv[1])


if __name__ == '__main__':
    main()
