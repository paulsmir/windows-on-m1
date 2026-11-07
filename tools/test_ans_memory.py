#!/usr/bin/env python3
"""Exercise raw production stage-2 accessor and complete DMA/PRP authorization."""
from pathlib import Path
import subprocess
import os
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.local/ans-native/tests'
OUT.mkdir(parents=True,exist_ok=True)
s=(ROOT/'m1n1_windows/src/hv_vm.c').read_text()
macros=s[s.index('#define PAGE_SIZE'):s.index('uint64_t vaddr_bits;')]
start=s.index('bool hv_stage2_get_leaf(')
opening=s.index('{',start);depth=1;end=opening+1
while depth:
    depth+=(s[end]=='{')-(s[end]=='}');end+=1
prelude='''#include <stdint.h>
#include <stdbool.h>
typedef uint64_t u64; typedef uint8_t u8;
#define BIT(n) (UINT64_C(1) << (n))
#define MASK(n) (BIT(n)-1)
#define GENMASK(hi,lo) ((UINT64_MAX >> (63-(hi))) & (UINT64_MAX << (lo)))
#define FIELD_GET(mask,val) (((val)&(mask)) >> __builtin_ctzll(mask))
static u64 *hv_Ltop;
static uint64_t vaddr_bits;
'''
header=OUT/'stage2-accessor.h';header.write_text(prelude+macros+s[start:end]+'\n')
exe=OUT/'memory'
subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
 '-fsanitize=address,undefined','-fno-omit-frame-pointer',
 '-DANS_TEST_STAGE2_SOURCE="'+str(header)+'"',
 'm1n1_windows/src/hv_ans_memory.c','m1n1_windows/src/hv_ans_prp.c',
 'm1n1_windows/tests/hv_ans_memory_test.c','-o',str(exe)],cwd=ROOT,check=True)
subprocess.run([str(exe)],check=True)
