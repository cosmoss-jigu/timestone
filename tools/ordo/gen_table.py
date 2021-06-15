#!/usr/bin/env python2

import os
import subprocess

from itertools import combinations

import gen_cpuseq

iters = 1000000


def sh(*args):
    out = subprocess.check_output(args)
    #return out.decode("utf8").strip()
    return out.strip()


def get_cores():
    return [int(cpu["processor"]) for cpu in gen_cpuseq.seq(gen_cpuseq.cpuinfo)]


def __get_values(a, b):
    tok = sh("o/reftable", str(a), str(b), str(iters)).split(" ")
    tok = tok[0].split('\n')
    tok = map(lambda x: int(x), tok)
    f = open('output/%d-%d.txt' % (a, b), 'w')
    for i in tok:
        f.write("%d\n" % i)
    f.close()
    return min(tok)


def build_table(a, b):
    return min(__get_values(a, b), __get_values(b, a))

def __ensure_output_dir():
    output_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'output')
    if os.path.isdir(output_path):
        return
    os.makedirs(output_path)

max_offset = 0
__ensure_output_dir()
for (a, b) in combinations(get_cores(), 2):
    key = (min(a, b), max(a, b))
    max_offset = max(build_table(*key), max_offset)
    print ("(%d, %d) ... done" % (a, b))
print (max_offset)
