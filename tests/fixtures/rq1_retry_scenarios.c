/* RQ-1 scenarios (appended to the vsync replay harness): a swap the broker
 * fails after acceptance is retried from the vertical tick, its PresentIds
 * are reported exactly once (latched, or after the last try), the gate is
 * released only then, and the held set goes after the retry resolves. */
static void period(ADMISSION_SCANOUT_RUNTIME*r){ULONGLONG d;assert(AppleAgxVsyncDeadline(&r->Timeline,&d));fakeNow=d;fakeIrql=2;AdmissionScanoutTimerDpc(&r->VsyncDpc,r,0,0);fakeIrql=0;}
static unsigned fres, fcommits, frefuse;
static APPLE_AGX_SCANOUT_U64 freceipt, freq;
static unsigned mpo_reports;
static APPLE_AGX_SCANOUT_BOOL br32(void*c,unsigned o,unsigned*v){(void)c;o-=APPLE_AGX_SCANOUT_MMIO_OFFSET;
  if(o==APPLE_AGX_SCANOUT_REG_IRQ_STATUS)*v=irq;else if(o==APPLE_AGX_SCANOUT_REG_RESULT)*v=fres;else return 0;return 1;}
static APPLE_AGX_SCANOUT_BOOL br64(void*c,unsigned o,APPLE_AGX_SCANOUT_U64*v){(void)c;o-=APPLE_AGX_SCANOUT_MMIO_OFFSET;
  if(o==APPLE_AGX_SCANOUT_REG_LATCHED_SEQUENCE)*v=latch;else if(o==APPLE_AGX_SCANOUT_REG_RECEIPT_SEQUENCE)*v=freceipt;else return 0;return 1;}
static APPLE_AGX_SCANOUT_BOOL bw32(void*c,unsigned o,unsigned v){(void)c;o-=APPLE_AGX_SCANOUT_MMIO_OFFSET;
  if(o==APPLE_AGX_SCANOUT_REG_IRQ_STATUS){irq&=~v;return 1;}
  if(o==APPLE_AGX_SCANOUT_REG_COMMAND){fcommits++;if(frefuse){fres=APPLE_AGX_SCANOUT_BROKER_PRESENT_FAILED;freceipt=freq;}else fres=0;return 1;}
  return 1;}
static APPLE_AGX_SCANOUT_BOOL bw64(void*c,unsigned o,APPLE_AGX_SCANOUT_U64 v){(void)c;o-=APPLE_AGX_SCANOUT_MMIO_OFFSET;
  if(o==APPLE_AGX_SCANOUT_REG_REQUEST_SEQUENCE)freq=v;return 1;}
static void count_mpo(void*c,DXGKARGCB_NOTIFY_INTERRUPT_DATA*d){notify(c,d);if(d->InterruptType==DXGK_INTERRUPT_CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY2)mpo_reports++;}
/* A two-layer flip (LayerIndex 0 id 100, LayerIndex 1 id 200) accepted as sequence Seq. */
static void v3_pending(ADMISSION_CONTEXT*c,ADMISSION_SCANOUT_RUNTIME*r,unsigned seq){
  (void)c;pending(r,seq,0x1500110000LL);irq=0;latch=0;
  r->Panel.Scanout.Io.Read32=br32;r->Panel.Scanout.Io.Read64=br64;r->Panel.Scanout.Io.Write32=bw32;r->Panel.Scanout.Io.Write64=bw64;
  r->Panel.Scanout.TwoLayers=1;r->Panel.Scanout.NextSequence=seq+1;
  r->PendingMpoCount=2;r->PendingMpoLayer[0]=0;r->PendingMpoLayer[1]=1;r->PendingMpoPresentId[0]=100;r->PendingMpoPresentId[1]=200;
  r->PendingMpoKind=ADMISSION_SCANOUT_MPO_KIND_TWO_LAYERS;
  r->RetryKind=ADMISSION_SCANOUT_RETRY_V3;r->RetryCount=2;r->RetrySequence=seq;r->RetryTries=0;
  memset(r->RetryRecords,0,sizeof(r->RetryRecords));r->RetryRecords[0].Width=r->RetryRecords[1].Width=64;
  fres=0;freceipt=seq;frefuse=0;fcommits=0;}
static void fail_pending(ADMISSION_CONTEXT*c,unsigned seq){fres=APPLE_AGX_SCANOUT_BROKER_PRESENT_FAILED;freceipt=seq;irq=APPLE_AGX_SCANOUT_IRQ_ERROR;AdmissionScanoutInterrupt(c);}
static void boot(ADMISSION_CONTEXT*c,ADMISSION_SCANOUT_RUNTIME*r){setup(c,r);assert(finish_start(c,r)==0);AdmissionScanoutControlInterrupt(c,1);
  pending(r,2,0x1500010000LL);AdmissionScanoutInterrupt(c);assert(!r->PendingValid&&!r->PresentGate);}
int main(void){ADMISSION_CONTEXT c;ADMISSION_SCANOUT_RUNTIME r;unsigned m,k;
/* A: failure -> retry on the tick -> the retry latches -> the original ids reported once. */
boot(&c,&r);c.Interface.DxgkCbNotifyInterrupt=count_mpo;
v3_pending(&c,&r,10);m=mpo_reports;commit_pending_calls=0;
fail_pending(&c,10);
assert(!r.Faulted&&!r.PendingValid&&r.PresentGate==1&&r.RetryArmed==1&&c.Mpo.SwapFailures==1&&c.Mpo.LastSwapFailureResult==APPLE_AGX_SCANOUT_BROKER_PRESENT_FAILED);
assert(!r.Panel.Scanout.PresentPending&&mpo_reports==m&&r.PendingMpoPresentId[0]==100&&r.PendingMpoPresentId[1]==200);
period(&r);
assert(fcommits==1&&r.PendingValid&&r.PendingSequence==11&&r.RetryTries==1&&c.Mpo.SwapRetries==1&&!r.RetryArmed&&r.PresentGate==1);
assert(commit_pending_calls==0); /* the gate is held: no held set goes */
irq=APPLE_AGX_SCANOUT_IRQ_LATCHED;latch=11;AdmissionScanoutInterrupt(&c);
assert(mpo_reports==m+1&&last_mpo_count==2&&last_mpo_id[0]==100&&last_mpo_id[1]==200&&c.Mpo.SwapRetryLatched==1&&r.RetryTries==0);
assert(r.PresentGate==0&&!r.PendingValid&&commit_pending_calls==1); /* the held set goes after the retry resolved */
period(&r);assert(mpo_reports==m+1); /* reported once */
puts("RQ-1 failed swap retried, latched, ids once PASS");
/* B: three failed retries -> ids reported exactly once at the next vsync -> gate released -> the next commit reports only its own ids. */
boot(&c,&r);c.Interface.DxgkCbNotifyInterrupt=count_mpo;
v3_pending(&c,&r,20);m=mpo_reports;commit_pending_calls=0;
fail_pending(&c,20);frefuse=1;
for(k=0;k<3;++k){period(&r);assert(r.RetryTries==(LONG)(k+1)&&r.PresentGate==1&&!r.PendingValid&&mpo_reports==m&&commit_pending_calls==0);}
assert(fcommits==3&&c.Mpo.SwapRetries==3&&!r.Faulted);
period(&r);
assert(c.Mpo.SwapRetryExhaustedReported==1&&mpo_reports==m+1&&last_mpo_count==2&&last_mpo_id[0]==100&&last_mpo_id[1]==200);
assert(r.PresentGate==0&&!r.RetryArmed&&r.RetryKind==ADMISSION_SCANOUT_RETRY_NONE&&!r.PlaneOffPending&&commit_pending_calls==1);
period(&r);assert(mpo_reports==m+1); /* exactly once */
frefuse=0;pending(&r,30,0x1500210000LL);r.Panel.Scanout.Io.Read32=br32;r.Panel.Scanout.Io.Read64=br64;r.Panel.Scanout.Io.Write32=bw32;
r.PendingMpoCount=1;r.PendingMpoLayer[0]=1;r.PendingMpoPresentId[0]=300;r.PendingMpoKind=ADMISSION_SCANOUT_MPO_KIND_TWO_LAYERS;
irq=APPLE_AGX_SCANOUT_IRQ_LATCHED;latch=30;AdmissionScanoutInterrupt(&c);
assert(mpo_reports==m+2&&last_mpo_count==1&&last_mpo_layer[0]==1&&last_mpo_id[0]==300);
puts("RQ-1 exhausted retry reported once, next commit own ids PASS");
/* C: an IRQ_ERROR with no present outstanding (a RELEASE failure) changes nothing. */
boot(&c,&r);
r.Panel.Scanout.Io.Read32=br32;r.Panel.Scanout.Io.Read64=br64;r.Panel.Scanout.Io.Write32=bw32;
fres=APPLE_AGX_SCANOUT_BROKER_PRESENT_FAILED;freceipt=5;irq=APPLE_AGX_SCANOUT_IRQ_ERROR;AdmissionScanoutInterrupt(&c);
assert(!r.Faulted&&c.Mpo.ForeignErrors==1&&!r.RetryArmed&&r.Timeline.Running);
puts("RQ-1 foreign error ignored PASS");
/* D: a failed present that was not an asynchronous submission (PASSIVE start/retire) keeps the fail-closed path. */
boot(&c,&r);
v3_pending(&c,&r,40);r.RetryKind=ADMISSION_SCANOUT_RETRY_NONE;
fail_pending(&c,40);
assert(r.Faulted&&!r.RetryArmed&&c.Mpo.SwapFailures==0);
puts("RQ-1 PASSIVE present failure stays fail-closed PASS");
/* E: stop while a retry is armed: no retry after Stopping. */
boot(&c,&r);
v3_pending(&c,&r,50);fail_pending(&c,50);assert(r.RetryArmed);
r.Stopping=1;period(&r);assert(fcommits==0&&r.RetryTries==0);
puts("RQ-1 no retry once stopping PASS");
return 0;}
