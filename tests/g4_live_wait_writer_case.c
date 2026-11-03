static ADMISSION_G3_LIVE_WAIT_RECEIPT last;
static int inject;
static void AdmissionPagingReceiptWrite(ADMISSION_CONTEXT *a,PCWSTR name,
    const VOID *data,ULONG bytes,BOOLEAN flush) {
  ADMISSION_PAGING_RECEIPTS *r=a->PagingReceipts;
  assert(!pthread_mutex_trylock(&r->Lock));pthread_mutex_unlock(&r->Lock);
  assert(!wcscmp(name,L"Wom1G3LivePagingWait") && !flush && bytes==sizeof(last));
  /* Kernel registry calls must not inherit a new 3912-byte stack snapshot. */
  assert((uintptr_t)data >= (uintptr_t)r &&
      (uintptr_t)data + bytes <= (uintptr_t)r + sizeof(*r));
  memcpy(&last,data,sizeof(last));++writes;
  ULONGLONG sum=0;for(unsigned i=0;i<37;++i)sum+=last.Histogram[i];
  assert(sum==last.Total && last.Overwritten==last.Total-16);
  for(unsigned i=0;i<16;++i){const ADMISSION_G3_LIVE_WAIT_ENTRY *e=&last.Entries[i];
    assert(e->RangeBytes==e->Va+13 && e->Sequence>last.Total-16 && e->Sequence<=last.Total);}
  if(inject){ADMISSION_G3_LIVE_WAIT_ENTRY e={0};inject=0;
    e.Reason=6;e.Va=123;e.RangeBytes=136;e.Iterations=1;AdmissionRecordG3LiveWait(a,&e);}
}
static void *stage_many(void *opaque){ADMISSION_CONTEXT *a=opaque;
  for(unsigned i=0;i<1000;++i){ADMISSION_G3_LIVE_WAIT_ENTRY e={0};e.Reason=1+i%36;
    e.Va=i;e.RangeBytes=i+13;e.Iterations=1;AdmissionRecordG3LiveWait(a,&e);}return NULL;}
int main(void){ADMISSION_PAGING_RECEIPTS r={0};ADMISSION_CONTEXT a={0};
  a.PagingReceipts=&r;a.PhysicalDeviceObject=&a;pthread_mutex_init(&r.Lock,NULL);
  ADMISSION_G3_LIVE_WAIT_ENTRY e={0};AdmissionRecordG3LiveWait(&a,&e);assert(!queues);
  for(unsigned i=0;i<37;++i){e.Reason=i;e.Va=100+i;e.RangeBytes=113+i;e.Iterations=1;
    AdmissionRecordG3LiveWait(&a,&e);}
  assert(queues==1 && writes==0 && r.LiveWait.Total==37 && r.LiveWait.Overwritten==21);
  for(unsigned i=0;i<37;++i)assert(r.LiveWait.Histogram[i]==1);
  inject=1;queued_worker(NULL,&a);assert(writes==2 && last.Total==38 && !r.Queued && r.Idle);
  pthread_t t[2];for(unsigned i=0;i<2;++i)assert(!pthread_create(t+i,NULL,stage_many,&a));
  for(unsigned i=0;i<2;++i)assert(!pthread_join(t[i],NULL));
  assert(queues==2 && writes==2);queued_worker(NULL,&a);assert(last.Total==2038);
  r.Stopping=1;AdmissionRecordG3LiveWait(&a,&e);assert(queues==2 && r.LiveWait.Total==2038);
  pthread_mutex_destroy(&r.Lock);return 0;}
