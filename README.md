# Durable Transactional Memory Can Scale with Timestone (ASPLOS-2020, NVMW-2020)
===================================================

## Directory structure
```{.sh}
timestone
├── include             # public headers of timestone
├── lib                 #timestone library
├── lib++               #timestone C++ APIs
├── unittest			#Unittest with linked list and B+Tree example
├── benchmark           # benchmark
│   ├── rlu             #  - rlu implementation and benchmark
│   └── versioning      #  - versioned programming and benchmark
├── tools               # misc build tools
```

## Compile Timestone
```{.sh}
cd timestone/
make
```
## Run Bechamarks
```{.sh}
cd timestone/benchmark
sudo ./bench-timestone -b <no of buckets for hash table, set to 1 for linked list>
-d <duration(ms)> -i <initialize> -u <update ratio-- 20/200/800> 
-n<number of threads>

### sample
sudo ./bench-timestone -b 1000 -d 10000 -i 100 -u 200 -n 16
```
## Run Unittest
```{.sh}
cd timestone/lib/
CONF=gtest make
cd ../unittest/
make
sudo ./ut-ts --gtest_filter=cpp.btree_concurrent
```
## Contact 
Please contact us at madhavakrishnan@vt.edu and changwoo@vt.edu 
