#!/bin/sh
# usage: ./build.sh [stage] [suffix] [diag 0|1]  -> coop<suffix>.bin / coop<suffix>.sym (stage 0, 1 or 2; default 2)
set -e
STAGE=${1:-2}; SUF=${2:-}; DIAGSW=${3:-0}
gcc -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
    -mno-sse -mno-mmx -Os -Wall -Wextra -DSTAGE=$STAGE -DCOOP_DIAG=$DIAGSW -c coop_m1.c -o coop_m1.o
ld -m elf_i386 -z noseparate-code -T link.ld -o coop$SUF.elf coop_m1.o
objcopy -O binary coop$SUF.elf coop$SUF.bin
nm coop$SUF.elf > coop$SUF.sym
