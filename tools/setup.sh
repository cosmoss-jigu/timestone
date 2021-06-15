#!/bin/bash

CUR_DIR=$(realpath $( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )/)
source $CUR_DIR/config.sh

[[ ":$PATH:" != *":${BIN_DIR}/bin:"* ]] && export PATH=$BIN_DIR/bin:$PATH
alias vm-boot='make -C $PROJ_DIR vm-boot'
alias vm-fboot='make -C $PROJ_DIR vm-fboot'
alias vm-oboot='make -C $PROJ_DIR vm-oboot'
alias vm-ssh='make -C $PROJ_DIR vm-ssh'
alias vm-off='make -C $PROJ_DIR vm-off'
alias vm-gdb='make -C $PROJ_DIR vm-gdb'
alias vm-dmesg='make -C $PROJ_DIR vm-dmesg'
