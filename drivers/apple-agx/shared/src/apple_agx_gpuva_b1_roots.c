#include "apple_agx_gpuva_b1_roots.h"
#include <string.h>
#define B1_PAGE 0x4000ULL
#define B1_ROOT_INDEX 1u
#define B1_ALIAS_L1 128u
#define B1_OUTPUT_L1 640u
#define B1_BACKEND_L1 641u

static APPLE_AGX_BOOL request(APPLE_AGX_GPUVA_V5_CLIENT *client,
    APPLE_AGX_GPUVA_B1_ROOT *root, AGX_GPUVA_V5_REQUEST *q)
{
  AGX_GPUVA_V5_RESPONSE r={0};
  q->ProcessId=root->Input.ProcessId;
  q->ProcessGeneration=root->Input.Generation;
  if (!AppleAgxGpuvaV5ClientCall(client,q,&r)) {
    root->LastStatus=0xffffffffu;
    return APPLE_AGX_FALSE;
  }
  root->LastStatus=r.Status;
  return r.Status==0u && r.Flags==0u ? APPLE_AGX_TRUE : APPLE_AGX_FALSE;
}
static unsigned long long table_for(const APPLE_AGX_GPUVA_B1_ROOT *root,
                                    unsigned long long va)
{
  unsigned index=(unsigned)((va>>25)&2047u);
  return index==B1_BACKEND_L1 ? root->Input.BackendL2Ipa :
         index==B1_ALIAS_L1 ? root->Input.AliasL2Ipa :
         index==B1_OUTPUT_L1 ? root->Input.OutputL2Ipa : 0ULL;
}
static APPLE_AGX_BOOL parent(APPLE_AGX_GPUVA_V5_CLIENT *client,
    APPLE_AGX_GPUVA_B1_ROOT *root, unsigned index, APPLE_AGX_BOOL link)
{
  AGX_GPUVA_V5_REQUEST q={0};
  static const unsigned l1_index[4]={B1_ROOT_INDEX,B1_BACKEND_L1,
                                      B1_ALIAS_L1,B1_OUTPUT_L1};
  const unsigned long long child[4]={root->Input.L1Ipa,root->Input.BackendL2Ipa,
      root->Input.AliasL2Ipa,root->Input.OutputL2Ipa};
  if(index>=4u) return APPLE_AGX_FALSE;
  q.Command=AGX_GPUVA_V5_UPDATE_PARENT;
  q.TableIpa=index==0u ? root->Input.RootIpa : root->Input.L1Ipa;
  q.Index=l1_index[index];
  q.AuxIpa=link ? child[index] : 0ULL;
  return request(client,root,&q);
}
static APPLE_AGX_BOOL table(APPLE_AGX_GPUVA_V5_CLIENT *client,
    APPLE_AGX_GPUVA_B1_ROOT *root, unsigned index, APPLE_AGX_BOOL add)
{
  AGX_GPUVA_V5_REQUEST q={0};
  const unsigned long long ipa[4]={root->Input.L1Ipa,root->Input.BackendL2Ipa,
      root->Input.AliasL2Ipa,root->Input.OutputL2Ipa};
  if(index>=4u) return APPLE_AGX_FALSE;
  q.Command=add ? AGX_GPUVA_V5_REGISTER_TABLE : AGX_GPUVA_V5_REVOKE_TABLE;
  q.AuxIpa=ipa[index];
  q.Index=index==0u ? 1u : 2u;
  return request(client,root,&q);
}
static APPLE_AGX_BOOL leaf(APPLE_AGX_GPUVA_V5_CLIENT *client,
    APPLE_AGX_GPUVA_B1_ROOT *root, const APPLE_AGX_GPUVA_B1_PAGE *page,
    APPLE_AGX_BOOL map)
{
  AGX_GPUVA_V5_REQUEST q={0};
  unsigned long long table_ipa=table_for(root,page->GpuVa);
  if(!table_ipa || ((page->GpuVa>>36)&7u)!=B1_ROOT_INDEX) return APPLE_AGX_FALSE;
  q.Command=AGX_GPUVA_V5_UPDATE_LEAF;
  q.TableIpa=table_ipa;
  q.Index=(unsigned int)((page->GpuVa>>14)&2047u);
  q.Flags=15u;
  if(map){
    q.AllocationGeneration=page->Shared ? root->Input.GraphGeneration :
                                         root->Input.OutputGeneration;
    q.ValidMask=15u; q.WritableMask=15u;
    for(unsigned i=0;i<4u;i++)q.LogicalIpa[i]=page->GuestIpa+i*0x1000ULL;
  }
  return request(client,root,&q);
}
static APPLE_AGX_BOOL grant(APPLE_AGX_GPUVA_V5_CLIENT *client,
    APPLE_AGX_GPUVA_B1_ROOT *root, const APPLE_AGX_GPUVA_B1_GRANT *g,
    APPLE_AGX_BOOL add)
{
  AGX_GPUVA_V5_REQUEST q={0};
  q.Command=add ? (g->Shared ? AGX_GPUVA_V5_REGISTER_SHARED_BACKING :
                              AGX_GPUVA_V5_REGISTER_BACKING) :
                  AGX_GPUVA_V5_REVOKE_BACKING;
  q.AuxIpa=g->Ipa; q.AllocationGeneration=g->Generation;
  return request(client,root,&q);
}
static APPLE_AGX_BOOL already_granted(const APPLE_AGX_GPUVA_B1_ROOT *root,
                                      unsigned long long ipa)
{
  for(unsigned i=0;i<root->GrantCount;i++)
    if(root->Grants[i].Ipa==ipa) return APPLE_AGX_TRUE;
  return APPLE_AGX_FALSE;
}
APPLE_AGX_BOOL AppleAgxGpuvaB1DestroyRoot(
    APPLE_AGX_GPUVA_V5_CLIENT *client, APPLE_AGX_GPUVA_B1_ROOT *root)
{
  AGX_GPUVA_V5_REQUEST q={0};
  if(!client || !root) return APPLE_AGX_FALSE;
  while(root->MappedCount){
    if(!leaf(client,root,&root->Mapped[root->MappedCount-1u],APPLE_AGX_FALSE))
      return APPLE_AGX_FALSE;
    --root->MappedCount;
  }
  while(root->GrantCount){
    if(!grant(client,root,&root->Grants[root->GrantCount-1u],APPLE_AGX_FALSE))
      return APPLE_AGX_FALSE;
    --root->GrantCount;
  }
  while(root->ParentCount){
    if(!parent(client,root,root->ParentCount-1u,APPLE_AGX_FALSE))
      return APPLE_AGX_FALSE;
    --root->ParentCount;
  }
  while(root->TableCount){
    if(!table(client,root,root->TableCount-1u,APPLE_AGX_FALSE))
      return APPLE_AGX_FALSE;
    --root->TableCount;
  }
  if(root->Created){
    q.Command=AGX_GPUVA_V5_DESTROY;
    if(!request(client,root,&q)) return APPLE_AGX_FALSE;
    root->Created=0u;
  }
  return APPLE_AGX_TRUE;
}
APPLE_AGX_BOOL AppleAgxGpuvaB1BuildRoot(
    APPLE_AGX_GPUVA_V5_CLIENT *client, const APPLE_AGX_GPUVA_B1_ROOT_INPUT *input,
    const APPLE_AGX_GPUVA_B1_PAGE *pages, unsigned int page_count,
    APPLE_AGX_GPUVA_B1_ROOT *root)
{
  AGX_GPUVA_V5_REQUEST q={0};
  const unsigned long long tables[5]={input ? input->RootIpa : 0,
      input ? input->L1Ipa : 0,input ? input->BackendL2Ipa : 0,
      input ? input->AliasL2Ipa : 0,input ? input->OutputL2Ipa : 0};
  if(!client || !input || !pages || !root || !input->ProcessId ||
     !input->Generation || !input->GraphGeneration || !input->OutputGeneration ||
     input->Paging>1u || page_count!=APPLE_AGX_GPUVA_B1_GRAPH_PAGES)
    return APPLE_AGX_FALSE;
  for(unsigned i=0;i<5u;i++){
    if(!tables[i] || (tables[i]&(B1_PAGE-1)))return APPLE_AGX_FALSE;
    for(unsigned j=0;j<i;j++)if(tables[i]==tables[j])return APPLE_AGX_FALSE;
  }
  memset(root,0,sizeof(*root)); root->Input=*input;
  q.Command=AGX_GPUVA_V5_CREATE; q.TableIpa=input->RootIpa;
  q.Flags=input->Paging;
  if(!request(client,root,&q))goto fail;
  root->Created=1u;
  for(unsigned i=0;i<4u;i++){
    if(!table(client,root,i,APPLE_AGX_TRUE))goto fail;
    ++root->TableCount;
  }
  for(unsigned i=0;i<4u;i++){
    if(!parent(client,root,i,APPLE_AGX_TRUE))goto fail;
    ++root->ParentCount;
  }
  for(unsigned i=0;i<page_count;i++){
    APPLE_AGX_GPUVA_B1_GRANT g={pages[i].GuestIpa,
      pages[i].Shared ? input->GraphGeneration : input->OutputGeneration,
      pages[i].Shared};
    if((pages[i].GpuVa&(B1_PAGE-1)) || (g.Ipa&(B1_PAGE-1)) ||
       !table_for(root,pages[i].GpuVa) || root->MappedCount>=302u)
      goto fail;
    if(!already_granted(root,g.Ipa)){
      if(root->GrantCount>=APPLE_AGX_GPUVA_B1_MAX_GRANTS ||
         !grant(client,root,&g,APPLE_AGX_TRUE))goto fail;
      root->Grants[root->GrantCount++]=g;
    }
    if(!leaf(client,root,&pages[i],APPLE_AGX_TRUE))goto fail;
    root->Mapped[root->MappedCount++]=pages[i];
  }
  return APPLE_AGX_TRUE;
fail:
  {
    unsigned int first_status=root->LastStatus;
    APPLE_AGX_BOOL cleaned=AppleAgxGpuvaB1DestroyRoot(client,root);
    root->CleanupStatus=cleaned ? 0u : root->LastStatus;
    root->LastStatus=first_status;
  }
  return APPLE_AGX_FALSE;
}
