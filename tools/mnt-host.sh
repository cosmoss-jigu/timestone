#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
# SPDX-License-Identifier: Apache-2.0

mkdir -p $HOME/host
sudo mount -t 9p -o trans=virtio,version=9p2000.L /dev/host $HOME/host
