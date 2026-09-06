#!/usr/bin/env python3
"""Run actual Windows reset/operation bodies against the actual EL2 service core."""
from pathlib import Path
import os, re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[5]
WIN=ROOT/'drivers/storage/appleans/windows'
text=(WIN/'native.c').read_text()
def function(name):
    match=re.search(r'^(?:static )?(?:int|BOOLEAN) '+name+r'\(',text,re.M)
    assert match,name
    start=match.start(); brace=text.index('{',match.end()); depth=1; end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}'); end+=1
    return text[start:end]
reset=function('AppleAnsNativeReset')
if os.environ.get('ANS_MUTATE_OLD_RESET'):
    begin=reset.index('    /* Healthy CLAIMED')
    end=reset.index('    if (operation(a,ANS_OP_RESET)',begin)
    reset=reset[:begin]+reset[end:]
source=r'''
#include "transport.h"
#include "telemetry.h"
#include "hv_ans_service.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
typedef bool BOOLEAN;
typedef uint32_t ULONG;
#define TRUE true
#define FALSE false
#define AppleAnsStageReset 19
#define SRB_STATUS_BUS_RESET 14
struct adapter { bool Ready,MayOwn; struct { unsigned ready; } Namespace;
    unsigned Stage,Failure; struct ans_wire Wire; struct ans_telemetry Stats; };
typedef struct adapter *PAPPLE_ANS_ADAPTER;
static struct ans_service svc;
static uint8_t mailbox[ANS_BROKER_SIZE];
static unsigned operations[16],count,stops,retired,restarts,releases;
static bool stop_ok;
static jmp_buf terminal;
static bool stop(void *p,uint32_t *stage){(void)p;(void)stage;stops++;return stop_ok;}
static bool backend_retire(void *p){(void)p;assert(stop_ok && stops);return true;}
static void release_tag(void *p,unsigned tag){(void)p;(void)tag;releases++;}
static ans_u32 read32(void *p,ans_u32 off){ans_u32 v;(void)p;
    if(off<sizeof(svc.broker.header))memcpy(&v,(uint8_t*)&svc.broker.header+off,4);
    else memcpy(&v,mailbox+off,4);return v;}
static ans_u64 read64(void *p,ans_u32 off){(void)p;(void)off;return 0;}
static void write32(void *p,ans_u32 off,ans_u32 value){(void)p;
    if(off!=ANS_DOORBELL_OFFSET){memcpy(mailbox+off,&value,4);return;}
    struct ans_broker_request r;struct ans_broker_response out;
    memcpy(&r,mailbox+ANS_REQUEST_OFFSET,sizeof(r));
    assert(value==(ans_u32)r.sequence);operations[count++]=r.operation;
    ans_service_execute(&svc,&r,&out);
    memcpy(mailbox+ANS_RESPONSE_OFFSET,&out,sizeof(out));}
static void barrier(void *p){(void)p;}
static const struct ans_wire_ops ops={read32,read64,write32,barrier};
static void terminal_containment(PAPPLE_ANS_ADAPTER a){(void)a;longjmp(terminal,1);}
static void retire(PAPPLE_ANS_ADAPTER a,unsigned status){
    assert(status==SRB_STATUS_BUS_RESET && stops && stop_ok && releases==ANS_QUEUE_DEPTH);
    assert(svc.broker.owner==ANS_OWNER_NONE && svc.broker.header.state==ANS_STATE_READY);
    retired++;a->MayOwn=false;}
static BOOLEAN AppleAnsNativeInitialize(PAPPLE_ANS_ADAPTER a){(void)a;
    assert(retired && svc.broker.header.sequence==0);restarts++;return true;}
'''+function('wire_call')+'\n'+function('operation')+'\n'+reset+r'''
static void setup(struct adapter *a){
    memset(a,0,sizeof(*a));memset(&svc,0,sizeof(svc));memset(mailbox,0,sizeof(mailbox));
    count=stops=retired=restarts=releases=0;stop_ok=true;
    svc.ops.stop=stop;svc.ops.retire=backend_retire;svc.ops.release_tag=release_tag;
    ans_broker_init(&svc.broker);
    assert(ans_broker_handoff(&svc.broker,true,true,true,true)==ANS_RESULT_OK);
    ans_stats_init(&a->Stats);
    assert(ans_wire_open(&a->Wire,&ops,NULL)==ANS_WIRE_OK);
    assert(operation(a,ANS_OP_CLAIM)==ANS_WIRE_OK);
    a->MayOwn=true;a->Ready=true;svc.admin_started=true;count=0;
}
int main(void){struct adapter a;setup(&a);
    /* The actual service rejects RESET from CLAIMED. No permissive fake. */
    assert(operation(&a,ANS_OP_RESET)==ANS_WIRE_REJECTED && !stops);
    count=0;
    if(setjmp(terminal)==0){assert(AppleAnsNativeReset(&a));}
    else assert(!"healthy reset incorrectly reached terminal containment");
    assert(count==2 && operations[0]==ANS_OP_QUIESCE && operations[1]==ANS_OP_RESET);
    assert(stops==1 && retired==1 && restarts==1);
    setup(&a);svc.broker.header.state=ANS_STATE_FAULTED;a.Wire.faulted=1;
    if(setjmp(terminal)==0){assert(AppleAnsNativeReset(&a));}
    else assert(!"faulted reset incorrectly reached terminal containment");
    assert(count==1 && operations[0]==ANS_OP_RESET && retired==1);
    setup(&a);stop_ok=false;
    if(setjmp(terminal)==0){(void)AppleAnsNativeReset(&a);assert(!"unsafe reset returned");}
    assert(stops==1 && !retired && !restarts && !releases && a.MayOwn);
    puts("actual Windows reset + EL2 service ordering: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='ans-reset-') as folder:
    path=Path(folder)/'test.c';path.write_text(source);exe=Path(folder)/'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-iquote',str(WIN),'-iquote',str(ROOT/'m1n1_windows/src'),str(path),
        str(WIN/'transport.c'),str(WIN/'telemetry.c'),
        str(ROOT/'m1n1_windows/src/hv_ans_service.c'),
        str(ROOT/'m1n1_windows/src/hv_ans_broker.c'),
        str(ROOT/'m1n1_windows/src/hv_ans_prp.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
