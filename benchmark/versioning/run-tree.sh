#!/bin/bash

#threads="1 4 8 14 28 56 84 112 140 168 196 224 280 336 392 448"
threads="2 8 14 28 56 84 140 196 280 392 448"
data="10000"

echo "####################################"
echo "Evaluation Tree 10K "
for thread in ${threads}
do
    echo "run bench-timestone num thread ${thread}"
#    ./benchmark_tree_mvrlu -i10000 -r20000 -d5000 -u20 -n${thread}
done

echo "####################################"
echo "Evaluation Tree 10K "
for thread in ${threads}
do
    echo "run bench-timestone num thread ${thread}"
 #   ./benchmark_tree_mvrlu -i10000 -r20000 -d5000 -u200 -n${thread}
done

echo "####################################"
echo "Evaluation Tree 10K "
for thread in ${threads}
do
    echo "run bench-timestone num thread ${thread}"
    ./benchmark_tree_mvrlu -i10000 -r20000 -d5000 -u800 -n${thread}
done
