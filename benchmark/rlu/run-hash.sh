#!/bin/bash

#threads="1 4 8 14 28 56 84 112 140 168 196 224 280 336 392 448"
threads="1 8 16 24 32 40 48 60 78 96"
data="10000"

echo "####################################"
echo "####################################"
echo "Evaluation Hash 10K "
for thread in ${threads}
do
    echo "run bench-timestone num thread ${thread}"
   #./bench-timestone -b1000 -i10000 -r20000 -d10000 -n${thread}
   ./bench-timestone -j/mnt/pmem/nvheap -k24 -b1000 -i10000 -r20000 -d10000 -n${thread}
done

echo "####################################"
echo "####################################"
echo "Evaluation Hash 100K "
for thread in ${threads}
do
    echo "run bench-timestone num thread ${thread}"
   #./bench-timestone -i1000 -r2000 -d10000 -n${thread}
   #./bench-timestone -j/mnt/pmem/nvheap -k24 -b10000 -i100000 -r200000 -d7000 -n${thread}
done
