#!/usr/bin/env python3

import os
import sys
import numpy

offset_table = []


def get_freq(a):
    v = {}
    for i in a:
        if i in v:
            v[i] += 1
        else:
            v.update({i: 1})
    return v


def get_freq_table(id):
    a = offset_table[id-1][1]
    freq_table = get_freq(a)
    with open("freq_table.0-%d" % id, "w") as f:
        for k, v in sorted(freq_table.items()):
            f.write("%s, %s\n" % (str(k), str(v)))


def process_file(filename, cid):
    a = []
    v = []
    ts1 = 0
    ts2 = 0
    old_offset = 0
    count = 0
    total = 0
    min = 100000
    max = -1000000
    freq = {}
    min_rtt = 10000000
    with open(filename, "r") as f:
        for i in f.readlines():
            total += 1
            v = i.split(' ')
            if ts1 == 0:
                ts1 = int(v[3])
                ts2 = int(v[5])
            else:
                ts1 = int(v[3]) - old_offset
                ts2 = int(v[5]) - old_offset
            ratio = int((ts1 - int(v[1])) * 100 / (int(v[7]) - ts2))
            offset = float((ts1 - int(v[1])) + (ts2 - int(v[7])))/2
            tms = ts1 - int(v[1])
            tsm = int(v[7]) - ts2
            tsdiff = ts2 - int(v[3])
            x = int(v[3]) - int(v[1])
            if min_rtt > x:
                min_rtt = x
            v = (tms + tsm, tms, tsm, tsdiff, offset)
            if ratio > 97 and ratio < 103:
                count += 1
                print("earlier: %d, now: %d, offset: %d ratio: %d" %
                      (ts1, ts1 - offset, offset, ratio))
                old_offset = offset
                if min > old_offset:
                    min = old_offset
                if max < old_offset:
                    max = old_offset
            if ratio in freq:
                freq[ratio] += 1
            else:
                freq.update({ratio: 1})
            a.append(v)
    for k, v in sorted(freq.items()):
        print("%d %d" % (k, v))
    print("min: %d max: %d min_rtt: %d" % (min, max, min_rtt))
    a.sort()
    offset_table.append((i, a))
    # with open("offsettable", "a") as f:
    #     f.write("===== %d =====\n" % cid)
    #     for i in a:
    #         f.write("%d %d %d %d %f\n" % (i[0], i[1], i[2], i[3], i[4]))


def main():
    if len(sys.argv) < 3:
        print("%s output num_cores" % sys.argv[0])
        return -1

    num_cores = int(sys.argv[2])
    for i in range(1, num_cores):
        filename = os.path.join(sys.argv[1], "0-1")
        process_file(filename, i)
        # print("=========")
        # filename = os.path.join(sys.argv[1], "45-0")
        # process_file(filename, i)
        return -1

    # print("1")
    # get_freq_table(1)
    # print("15")
    # get_freq_table(15)
    # print("45")
    # get_freq_table(45)


if __name__ == '__main__':
    main()
