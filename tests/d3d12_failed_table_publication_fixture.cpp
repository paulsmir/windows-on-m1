static unsigned checks,failures;
static void check(bool yes,const char*why){++checks;if(!yes){++failures;std::printf("TABLE_PUBLICATION_FAIL %s\n",why);}}
template<class T>struct Guarded{
 alignas(T) unsigned char pre[64];T table;unsigned char post[64];
 Guarded(){std::memset(this,0xa5,sizeof(*this));}
 bool guards()const{for(unsigned char x:pre)if(x!=0xa5)return false;for(unsigned char x:post)if(x!=0xa5)return false;return true;}
};
static void slots(const D3D12DDI_COMMAND_LIST_FUNCS_3D_0003&l,const D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001&q){
/* ACTUAL_SLOT_CONTROLS */
 check(q.pfnUnused==nullptr&&q.pfnUnused2==nullptr,"two queue reserved slots null");
}
static int run(){
 static_assert(sizeof(void*)==8,"native x64 pointers");
 static_assert(sizeof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003)==408,"actual WDK list extent");
 static_assert(sizeof(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001)==56,"actual WDK queue extent");
 static_assert(std::is_same_v<decltype(&FillDDITable),PFND3D12DDI_FILLDDITTABLE>,"actual callback ABI");
 int bad_storage=0,h0_storage=0,h1_storage=0,queue_storage=0;
 const D3D12DDI_HRTTABLE bad{&bad_storage},h0{&h0_storage},h1{&h1_storage},qh{&queue_storage};
 for(unsigned short_case=0;short_case<2;short_case++){
  list_table={};Guarded<D3D12DDI_COMMAND_LIST_FUNCS_3D_0003>first,second;
  Guarded<D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001>queue;
  unsigned char pristine[sizeof(first)];std::memcpy(pristine,&first,sizeof(first));
  void*buffer=short_case?static_cast<void*>(&first.table):nullptr;
  SIZE_T size=short_case?sizeof(first.table)-1:sizeof(first.table);
  const HRESULT refused=FillDDITable({},D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D,buffer,size,0,bad);
  check(refused==E_INVALIDARG,"null/short request refused");
  check(!std::memcmp(pristine,&first,sizeof(first)),"failed Fill writes no supplied bytes/canaries");
  check(list_table.handle==nullptr,"failed first Fill does not publish tokenBad");
  check(FillDDITable({},D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D,&first.table,sizeof(first.table),0,h0)==S_OK,"valid list index0");
  check(list_table.handle==h0.handle,"first successful Fill publishes H0");
  check(FillDDITable({},D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D,&second.table,sizeof(second.table),1,h1)==S_OK,"valid list index1 same ABI");
  check(list_table.handle==h0.handle,"existing first-valid token policy retained after index1");
  const auto before_queue=list_table;
  check(FillDDITable({},D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D,&queue.table,sizeof(queue.table),0,qh)==S_OK,"valid queue index0");
  check(list_table.handle==before_queue.handle,"queue fill does not mutate list token");
  check(first.guards()&&second.guards()&&queue.guards(),"actual 408/56 writes preserve external canaries");
  check(!std::memcmp(&first.table,&second.table,sizeof(first.table)),"list indices0/1 fill identical typed callback identities");
  slots(first.table,queue.table);
 }
 check(!shim_invocations&&!core_invocations,"no GPU/callback/CORE body invoked");
 std::printf("ACTUAL_FAILED_TABLE_PUBLICATION checks=%u failures=%u list_slots=51 queue_slots=7 candidate_scope=first_success_only cross_device_fix=NO\n",checks,failures);
 return failures?1:0;
}
