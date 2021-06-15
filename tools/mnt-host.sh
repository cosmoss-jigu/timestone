#!/bin/bash

mkdir -p $HOME/host
sudo mount -t 9p -o trans=virtio,version=9p2000.L /dev/host $HOME/host
