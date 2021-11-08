#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
# SPDX-License-Identifier: Apache-2.0

CUR_DIR=$(realpath $( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )/)
export PROJ_DIR=$(realpath $CUR_DIR/..)
export BIN_DIR=$PROJ_DIR/bin
export KNL_DIR=$PROJ_DIR/linux
