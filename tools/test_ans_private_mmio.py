#!/usr/bin/env python3
"""Execute actual mapper with synthetic ADT; catch exposed ASC mailbox windows."""
from pathlib import Path
import re,subprocess,os
R=Path(__file__).resolve().parents[1]; O=R/'.local/ans-native/tests'; O.mkdir(parents=True,exist_ok=True)
s=(R/'m1n1_windows/src/hv_ans_platform.c').read_text();start=s.index('#define ANS_REJECT(why)');b=s.index('{',s.index('bool hv_ans_direct(',start));end=b+1;depth=1
while depth: depth+=(s[end]=='{')-(s[end]=='}');end+=1
assert s.index('bool hv_map_ans_broker(',start)<s.index('bool hv_ans_write_window(',start)<b  # slice holds the mapper, the window and the direct entry
asc=(R/'m1n1_windows/src/asc.c').read_text()
assert 'asc->base = base + 0x8000;' in asc
regs=[int(v,16) for n,v in re.findall(r'#define (ASC_MBOX_(?:A2I|I2A)_\w+)\s+(0x[0-9a-fA-F]+)',asc)]
pre='''#include "hv_ans_service.h"
#include <assert.h>
#include <stdio.h>
typedef uint64_t u64; typedef uint32_t u32;
#define T8103 0x8103
/* Live J313 ADT /arm-io/ans reg0/reg3 sizes (ANSDART001-R3 adt.bin; ANS001-R3 attempt 4
 * rejected the DT-sized expectation on hardware). */
static u64 adt_coprocessor_size=0x6c000,adt_controller_size=0x60000;
static u32 chip_id=T8103;static void *adt;
static struct {bool mapped;struct ans_service service;unsigned active_tag;u32 physical_irq;} runtime;
static bool guest_started;static unsigned window_calls,direct_calls;static bool direct_ok=true;
static bool hv_guest_has_started(void){return guest_started;}
bool ans_service_set_write_window(struct ans_service*s,u64 f,u64 n){(void)s;window_calls++;return n&&f<1000&&n<=1000-f;}
bool ans_service_set_direct(struct ans_service*s){(void)s;direct_calls++;return direct_ok;}
static int adt_path_offset_trace(void*a,const char*p,int*out){(void)a;out[0]=p[8]=='s'?2:1;return 1;}
static int adt_get_reg(void*a,int*p,const char*n,int i,u64*b,u64*s){(void)a;(void)n;
 if(p[0]==2){*b=ANS_EXPECTED_SART_BASE;*s=ANS_EXPECTED_SART_SIZE;}
 else if(i==3){*b=ANS_EXPECTED_CONTROLLER_BASE;*s=adt_controller_size;}
 else{*b=ANS_EXPECTED_COPROCESSOR_BASE;*s=adt_coprocessor_size;}return 0;}
static const u32 irq=ANS_EXPECTED_PHYSICAL_IRQ;
static const void*adt_getprop(void*a,int n,const char*p,u32*l){(void)a;(void)n;(void)p;*l=4;return &irq;}
static bool hv_nvme_recovery_geometry(u64*b){*b=1000;return true;}
bool ans_service_init(struct ans_service*s,const struct ans_service_ops*o,void*c,const struct ans_memory_ops*m,void*d,u64 b){(void)s;(void)o;(void)c;(void)m;(void)d;(void)b;return true;}
static bool hv_irq_route_register(u32 p,u32 v,bool l){return p==irq&&v==ANS_GUEST_GSIV&&l;}
static struct {u64 base,size;} maps[16];static unsigned count;
static int hv_map_hook(u64 b,void*hook,u64 n){(void)hook;assert(count<16);maps[count].base=b;maps[count++].size=n;return 0;}
#define freeze NULL
#define drain NULL
#define flush NULL
#define stop NULL
#define retire NULL
#define hide NULL
#define boot NULL
#define backing NULL
#define submit NULL
#define poll NULL
#define shadow NULL
#define release NULL
#define direct_release NULL
#define translate NULL
#define read_metadata NULL
#define private_mmio NULL
#define broker_mmio NULL
'''
post='''
static bool covered(u64 a,u64 n){for(unsigned i=0;i<count;i++)if(a>=maps[i].base&&n<=maps[i].size&&a-maps[i].base<=maps[i].size-n)return true;return false;}
int main(void){
 adt_coprocessor_size=ANS_EXPECTED_COPROCESSOR_SIZE;adt_controller_size=ANS_EXPECTED_CONTROLLER_SIZE;
 assert(!hv_map_ans_broker(ANS_BROKER_BASE,ANS_BROKER_SIZE,ANS_GUEST_GSIV,ANS_ABI_MAJOR)&&!runtime.mapped&&count==0);
 adt_coprocessor_size=0x6c000;adt_controller_size=0x60000;
 assert(hv_map_ans_broker(ANS_BROKER_BASE,ANS_BROKER_SIZE,ANS_GUEST_GSIV,ANS_ABI_MAJOR));
 assert(covered(ANS_EXPECTED_CONTROLLER_BASE,0x60000));assert(covered(ANS_EXPECTED_COPROCESSOR_BASE,0x6c000));
'''
for off in regs:post+=f'assert(covered(ANS_EXPECTED_COPROCESSOR_BASE+0x8000+{off},8));\n'
post+='''assert(covered(ANS_EXPECTED_MAILBOX_BASE,ANS_EXPECTED_MAILBOX_SIZE));
 /* ANS008 window entry: ABI and pre-guest state are checked before the service. */
 assert(!hv_ans_write_window(0,8,ANS_ABI_MAJOR+1)&&window_calls==0);
 guest_started=true;assert(!hv_ans_write_window(0,8,ANS_ABI_MAJOR)&&window_calls==0);guest_started=false;
 assert(!hv_ans_write_window(999,2,ANS_ABI_MAJOR)&&window_calls==1);
 assert(hv_ans_write_window(990,10,ANS_ABI_MAJOR)&&window_calls==2);
 /* ANS-DIRECT entry: ABI, mapped broker and pre-guest state before the service. */
 assert(!hv_ans_direct(ANS_ABI_MAJOR+1)&&direct_calls==0);
 guest_started=true;assert(!hv_ans_direct(ANS_ABI_MAJOR)&&direct_calls==0);guest_started=false;
 direct_ok=false;assert(!hv_ans_direct(ANS_ABI_MAJOR)&&direct_calls==1);direct_ok=true;
 assert(hv_ans_direct(ANS_ABI_MAJOR)&&direct_calls==2);
 runtime.mapped=false;assert(!hv_ans_write_window(0,8,ANS_ABI_MAJOR)&&window_calls==2);
 assert(!hv_ans_direct(ANS_ABI_MAJOR)&&direct_calls==2);
 puts("ANS private CPU/ASC mailbox mapping + write-window + direct entry tests PASS");}
'''
f=O/'private-mmio.c';f.write_text(pre+s[start:end]+post)
subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-I',str(R/'m1n1_windows/src'),str(f),'-o',str(O/'private-mmio')],check=True)
subprocess.run([str(O/'private-mmio')],check=True)
