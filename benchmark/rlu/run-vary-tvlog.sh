#!/bin/bash

benchs="x1 div2 div4 div8 div16"
threads="1 8 16 24 32 40 48 60 78 96"
thread="96"
size="10000"
buckets="10000"
range=`echo "2*${size}" | bc`
duration="70000"
seed="100"

echo "####################################"
echo "####################################"
echo "Evaluation Vary TVLog Hash10K "
for bench in ${benchs}
do
    echo "Run bench-timestone-${bench} thread ${thread}"
   ./bench-timestone-tvlog${bench} -b${buckets} -i${size} -r${range} -d${duration} -n${thread} -s${seed}
done
