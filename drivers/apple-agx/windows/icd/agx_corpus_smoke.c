/* DXBC corpus hardware smoke test. No DXIL input is accepted. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

#define MAX_SIG 64
#define MAX_FILES 65536
#define CB_WORDS (4096u * 4u)
#define REL(x) do { if (x) IUnknown_Release((IUnknown *)(x)); } while (0)
static volatile const char *current_file = "startup";
static volatile LONG shader_started;
static pD3DCompile compiler;
typedef struct { char name[128]; UINT index, sys, type, reg, mask, rw; } Sig;
typedef struct { Sig v[MAX_SIG]; UINT n; } Signature;
typedef struct { UINT dim, ret, samples; } ResourceDecl;
typedef struct { const unsigned char *bytes; size_t size; const unsigned char *program; size_t program_size; int stage, major, minor; Signature in, out; ResourceDecl resource[16]; int comparison[16]; int depth; char reason[96]; } Shader;
typedef struct { ID3D11Device *dev; ID3D11DeviceContext *ctx; } Device;
typedef struct { char label[64]; UINT pixel, checksum; DWORD ms; int hang, ok; } Run;
static void line(const char *fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); putchar('\n'); fflush(stdout); }
static LONG WINAPI crash(EXCEPTION_POINTERS *p) { line("CRASH code %08lx address %p at %s", p->ExceptionRecord->ExceptionCode, p->ExceptionRecord->ExceptionAddress, (const char *)current_file); return EXCEPTION_EXECUTE_HANDLER; }
static DWORD WINAPI watchdog(void *ignored) { (void)ignored; for (;;) { Sleep(1000); if (shader_started && (DWORD)(GetTickCount() - (DWORD)shader_started) > 20000) { line("WATCHDOG %s", (const char *)current_file); shader_started = 0; } } }
static UINT rd(const unsigned char *p) { return (UINT)p[0] | ((UINT)p[1]<<8) | ((UINT)p[2]<<16) | ((UINT)p[3]<<24); }
static int in_range(size_t off, size_t len, size_t total) { return off <= total && len <= total - off; }
static int four(const unsigned char *p, const char *s) { return memcmp(p,s,4)==0; }
static int parse_sig(Signature *sig, const unsigned char *p, size_t n, int modern, char *why) {
  UINT i, count, stride = modern ? 32 : 24;
  if (n < 8) return 0;
  count = rd(p); if (count > MAX_SIG || !in_range(8, (size_t)count*stride, n)) return 0;
  sig->n = count;
  for (i=0;i<count;i++) {
    const unsigned char *e=p+8+i*stride; UINT base=modern?4:0, off=rd(e+base), mask;
    Sig *s=&sig->v[i]; size_t end;
    if (!in_range(off,1,n)) return 0;
    for (end=off;end<n && p[end];end++); if (end==n || end-off>=sizeof(s->name)) return 0;
    memcpy(s->name,p+off,end-off); s->name[end-off]=0;
    s->index=rd(e+base+4); s->sys=rd(e+base+8); s->type=rd(e+base+12); s->reg=rd(e+base+16);
    mask=e[base+20]; s->mask=mask&15; s->rw=e[base+21]&15;
    if (!s->mask || (s->type<1 || s->type>3)) { strcpy(why,"signature type/mask"); return 0; }
  }
  return 1;
}
/* Declaration operands have one immediate 32-bit register index in the common SM4/5 form. */
static int declaration_slot(const unsigned char *p, UINT len, UINT *slot) {
  UINT op, dim, rep; if (len<3) return 0; op=rd(p+4); dim=(op>>20)&3; rep=(op>>22)&7;
  if (dim!=1 || rep!=0 || (op&0x80000000u)) return 0;
  *slot=rd(p+8); return *slot<16;
}
static int parse_program(Shader *s) {
  const unsigned char *p=s->program; UINT words, i;
  if (s->program_size<8) return 0;
  words=rd(p+4); if (words<2 || (size_t)words*4>s->program_size) return 0;
  s->stage=(rd(p)>>16)&0xffff; s->major=(rd(p)>>4)&15; s->minor=rd(p)&15;
  for (i=2;i<words;) {
    UINT tok=rd(p+i*4), op=tok&0x7ff, len=(tok>>24)&0x7f, slot;
    if (!len || i+len>words) return 0;
    if (op==88 || op==90) {
      if (!declaration_slot(p+i*4,len,&slot)) { strcpy(s->reason,"dynamic resource/sampler slot"); return 0; }
      if (op==88) { s->resource[slot].dim=(tok>>11)&31; s->resource[slot].samples=(tok>>16)&127; if (len<4) return 0; s->resource[slot].ret=rd(p+(i+3)*4)&15; }
      else s->comparison[slot]=(((tok>>11)&15)==1);
    }
    i+=len;
  }
  return 1;
}
static int parse(Shader *s, const unsigned char *p, size_t n) {
  UINT i, chunks; memset(s,0,sizeof(*s)); s->bytes=p; s->size=n; s->stage=-1;
  if (n<32 || !four(p,"DXBC") || rd(p+24)>n) return 0;
  chunks=rd(p+28); if (chunks>4096 || !in_range(32,(size_t)chunks*4,n)) return 0;
  for(i=0;i<chunks;i++) {
    UINT off=rd(p+32+i*4),sz; const unsigned char *c;
    if (!in_range(off,8,n)) return 0; c=p+off; sz=rd(c+4); if (!in_range(off+8,sz,n)) return 0;
    if (four(c,"SHDR") || four(c,"SHEX")) { s->program=c+8; s->program_size=sz; }
    else if (four(c,"ISGN") || four(c,"ISG1")) { if(!parse_sig(&s->in,c+8,sz,four(c,"ISG1"),s->reason)) return 0; }
    else if (four(c,"OSGN") || four(c,"OSG1") || four(c,"OSG5")) { if(!parse_sig(&s->out,c+8,sz,!four(c,"OSGN"),s->reason)) return 0; }
  }
  if (!s->program) return 0;
  if (!parse_program(s)) return 0;
  for(i=0;i<s->out.n;i++) if (_stricmp(s->out.v[i].name,"SV_Depth")==0) s->depth=1;
  return 1;
}
static int append(char *dst,size_t cap,const char *fmt,...) { size_t n=strlen(dst); int wrote; va_list a; if(n>=cap) return 0; va_start(a,fmt); wrote=vsnprintf(dst+n,cap-n,fmt,a); va_end(a); return wrote>=0 && (size_t)wrote<cap-n; }
static int comps(UINT mask) { int n=0; while(mask) { n+=mask&1; mask>>=1; } return n; }
static int position(const Sig *s) { return _stricmp(s->name,"SV_Position")==0 || s->sys==1; }
static int writable(const Sig *s) { return !(_strnicmp(s->name,"SV_",3)==0 && !position(s)); }
static const char *base_type(UINT t) { return t==1?"uint":t==2?"int":"float"; }
static int field(char *src,size_t cap,const Sig *s,UINT i,int is_ps_input) {
  const char *interp=(s->type!=3 && is_ps_input)?"nointerpolation ":"";
  return append(src,cap," %s%s%d f%u : %s%u;\n",interp,base_type(s->type),comps(s->mask),i,s->name,s->index);
}
static ID3DBlob *compile(const char *src,const char *target) {
  ID3DBlob *b=NULL,*err=NULL; HRESULT hr=compiler(src,strlen(src),NULL,NULL,NULL,"main",target,0,0,&b,&err);
  if(err) { line("COMPILE %s %.300s",target,(const char *)ID3D10Blob_GetBufferPointer(err)); ID3D10Blob_Release(err); }
  if(FAILED(hr)) { REL(b); return NULL; } return b;
}
static ID3DBlob *companion(const Shader *s) {
  char src[32768]={0},target[16]; const Signature *sig=s->stage==0?&s->in:&s->out; UINT i; int dep=-1, has_position=0;
  if(!append(src,sizeof(src),"struct V {\n")) return NULL;
  for(i=0;i<sig->n;i++) { if(position(&sig->v[i]))has_position=1; if((s->stage==1 || writable(&sig->v[i])) && !field(src,sizeof(src),&sig->v[i],i,s->stage==1)) return NULL; }
  if(s->stage==0 && !has_position && !append(src,sizeof(src)," float4 generated_position : SV_Position;\n"))return NULL;
  if(!append(src,sizeof(src),"};\n")) return NULL;
  if(s->stage==0) {
    if(!append(src,sizeof(src),"V main(uint id : SV_VertexID) { V v;\n")) return NULL;
    for(i=0;i<sig->n;i++) { const Sig *e=&sig->v[i]; int c=comps(e->mask),j;
      if(!writable(e)) continue;
      if(position(e)) { if(!append(src,sizeof(src),"v.f%u=float4(id==1?3.0:-1.0,id==2?-3.0:1.0,0,1);\n",i)) return NULL; }
      else { if(!append(src,sizeof(src),"v.f%u=%s%d(",i,base_type(e->type),c)) return NULL;
        for(j=0;j<c;j++) if(!append(src,sizeof(src),"%s%s",j?",":"",e->type==3?"0.25":"1")) return NULL;
        if(!append(src,sizeof(src),");\n")) return NULL; }
    }
    if(!has_position && !append(src,sizeof(src),"v.generated_position=float4(id==1?3.0:-1.0,id==2?-3.0:1.0,0,1);\n"))return NULL;
    if(!append(src,sizeof(src),"return v; }\n")) return NULL;
  } else {
    for(i=0;i<sig->n;i++) if(!position(&sig->v[i]) && writable(&sig->v[i])) { dep=(int)i; break; }
    if(!append(src,sizeof(src),"float4 main(V v) : SV_Target { float4 c=float4(0,0,0,1);\n")) return NULL;
    if(dep>=0 && !append(src,sizeof(src),"c.x += (float)v.f%d.x * 0.125;\n",dep)) return NULL;
    if(!append(src,sizeof(src),"return c; }\n")) return NULL;
  }
  snprintf(target,sizeof(target),"%s_%d_0",s->stage==0?"vs":"ps",s->major>=5?5:4);
  return compile(src,target);
}
static void close_device(Device *d) { REL(d->ctx); REL(d->dev); memset(d,0,sizeof(*d)); }
static HRESULT open_device(Device *d) {
  const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0}; D3D_FEATURE_LEVEL got;
  HRESULT hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,levels,3,D3D11_SDK_VERSION,&d->dev,&got,&d->ctx);
  if(FAILED(hr)) close_device(d); return hr;
}
static DXGI_FORMAT format(const Sig *e) {
  static const DXGI_FORMAT f[4][4]={{DXGI_FORMAT_UNKNOWN,DXGI_FORMAT_R32_UINT,DXGI_FORMAT_R32G32_UINT,DXGI_FORMAT_R32G32B32_UINT},
    {DXGI_FORMAT_UNKNOWN,DXGI_FORMAT_R32_SINT,DXGI_FORMAT_R32G32_SINT,DXGI_FORMAT_R32G32B32_SINT},
    {DXGI_FORMAT_UNKNOWN,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32G32_FLOAT,DXGI_FORMAT_R32G32B32_FLOAT}};
  int n=comps(e->mask); if(n==4) return e->type==1?DXGI_FORMAT_R32G32B32A32_UINT:e->type==2?DXGI_FORMAT_R32G32B32A32_SINT:DXGI_FORMAT_R32G32B32A32_FLOAT;
  return e->type>=1&&e->type<=3?f[e->type-1][n]:DXGI_FORMAT_UNKNOWN;
}
static HRESULT make_srv(Device *d,ResourceDecl r,ID3D11ShaderResourceView **view) {
  HRESULT hr; DXGI_FORMAT fmt=(r.ret==4)?DXGI_FORMAT_R32_UINT:(r.ret==3)?DXGI_FORMAT_R32_SINT:DXGI_FORMAT_R8G8B8A8_UNORM;
  if(r.dim==1) { D3D11_BUFFER_DESC b; ID3D11Buffer *buf=NULL; memset(&b,0,sizeof(b)); b.ByteWidth=64; b.Usage=D3D11_USAGE_DEFAULT; b.BindFlags=D3D11_BIND_SHADER_RESOURCE; hr=ID3D11Device_CreateBuffer(d->dev,&b,NULL,&buf); if(SUCCEEDED(hr)) { D3D11_SHADER_RESOURCE_VIEW_DESC v; memset(&v,0,sizeof(v)); v.Format=DXGI_FORMAT_R32_FLOAT; v.ViewDimension=D3D11_SRV_DIMENSION_BUFFER; v.Buffer.NumElements=16; hr=ID3D11Device_CreateShaderResourceView(d->dev,(ID3D11Resource *)buf,&v,view); } REL(buf); return hr; }
  if(r.dim==2 || r.dim==7) { D3D11_TEXTURE1D_DESC t; ID3D11Texture1D *tex=NULL; memset(&t,0,sizeof(t)); t.Width=4;t.MipLevels=1;t.ArraySize=r.dim==7?2:1;t.Format=fmt;t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_SHADER_RESOURCE; hr=ID3D11Device_CreateTexture1D(d->dev,&t,NULL,&tex); if(SUCCEEDED(hr)) hr=ID3D11Device_CreateShaderResourceView(d->dev,(ID3D11Resource *)tex,NULL,view); REL(tex); return hr; }
  if(r.dim==5) { D3D11_TEXTURE3D_DESC t; ID3D11Texture3D *tex=NULL; memset(&t,0,sizeof(t));t.Width=t.Height=t.Depth=2;t.MipLevels=1;t.Format=fmt;t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;hr=ID3D11Device_CreateTexture3D(d->dev,&t,NULL,&tex);if(SUCCEEDED(hr)) hr=ID3D11Device_CreateShaderResourceView(d->dev,(ID3D11Resource *)tex,NULL,view);REL(tex);return hr; }
  if(r.dim==3 || r.dim==4 || r.dim==6 || r.dim==8 || r.dim==9 || r.dim==10) {
    D3D11_TEXTURE2D_DESC t; ID3D11Texture2D *tex=NULL; memset(&t,0,sizeof(t));t.Width=t.Height=4;t.MipLevels=1;t.ArraySize=(r.dim==6)?6:(r.dim==10)?12:(r.dim==8||r.dim==9)?2:1;t.Format=fmt;t.SampleDesc.Count=(r.dim==4||r.dim==9)?(r.samples?r.samples:4):1;t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;if(r.dim==6||r.dim==10)t.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
    hr=ID3D11Device_CreateTexture2D(d->dev,&t,NULL,&tex);
    if(FAILED(hr) && t.SampleDesc.Count>1) { t.SampleDesc.Count=1; hr=ID3D11Device_CreateTexture2D(d->dev,&t,NULL,&tex); }
    if(SUCCEEDED(hr)) hr=ID3D11Device_CreateShaderResourceView(d->dev,(ID3D11Resource *)tex,NULL,view);REL(tex);return hr;
  }
  return E_NOTIMPL;
}
static void set_stage(Device *d,int stage,ID3D11Buffer **cb,ID3D11ShaderResourceView **srv,ID3D11SamplerState **sam) {
  if(stage==0) { ID3D11DeviceContext_PSSetConstantBuffers(d->ctx,0,14,cb);ID3D11DeviceContext_PSSetShaderResources(d->ctx,0,16,srv);ID3D11DeviceContext_PSSetSamplers(d->ctx,0,16,sam); }
  else { ID3D11DeviceContext_VSSetConstantBuffers(d->ctx,0,14,cb);ID3D11DeviceContext_VSSetShaderResources(d->ctx,0,16,srv);ID3D11DeviceContext_VSSetSamplers(d->ctx,0,16,sam); }
}
static void run(Device *d,const Shader *s,ID3DBlob *other,int nir,int zero,Run *out) {
  ID3D11VertexShader *vs=NULL; ID3D11PixelShader *ps=NULL; ID3D11InputLayout *layout=NULL; ID3D11Buffer *vb=NULL,*cb[14]={0}; ID3D11ShaderResourceView *srv[16]={0}; ID3D11SamplerState *sam[16]={0}; ID3D11Texture2D *rt[8]={0},*depth=NULL,*staging=NULL; ID3D11RenderTargetView *rtv[8]={0}; ID3D11DepthStencilView *dsv=NULL; ID3D11Query *query=NULL; D3D11_MAPPED_SUBRESOURCE mapped; D3D11_VIEWPORT viewport; D3D11_BUFFER_DESC bd; D3D11_TEXTURE2D_DESC td; D3D11_QUERY_DESC qd; D3D11_SAMPLER_DESC sd; D3D11_INPUT_ELEMENT_DESC elems[MAX_SIG]; UINT i,j,rt_count=1,stride=0,offset=0; int triangle_input=0; unsigned char vertices[3][1024]; float constants[CB_WORDS]; HRESULT hr=S_OK; DWORD start=GetTickCount(); const void *code=s->bytes; size_t bytes=s->size;
  memset(out,0,sizeof(*out)); strcpy(out->label,"SKIP"); memset(vertices,0,sizeof(vertices));
  if(!d->dev) { hr=open_device(d); if(FAILED(hr)) goto fail; }
  /* The environment is read by the UMD at each Create*Shader call. */
  if(!SetEnvironmentVariableA("APPLE_AGX_D3D_SHADER_NIR",nir?"1":NULL)) { hr=HRESULT_FROM_WIN32(GetLastError()); goto fail; }
  if(s->stage==0) {
    hr=ID3D11Device_CreatePixelShader(d->dev,code,bytes,NULL,&ps); if(FAILED(hr)) goto fail;
    hr=ID3D11Device_CreateVertexShader(d->dev,ID3D10Blob_GetBufferPointer(other),ID3D10Blob_GetBufferSize(other),NULL,&vs); if(FAILED(hr)) goto fail;
  } else {
    hr=ID3D11Device_CreateVertexShader(d->dev,code,bytes,NULL,&vs); if(FAILED(hr)) goto fail;
    hr=ID3D11Device_CreatePixelShader(d->dev,ID3D10Blob_GetBufferPointer(other),ID3D10Blob_GetBufferSize(other),NULL,&ps); if(FAILED(hr)) goto fail;
  }
  if(s->stage==1) {
    for(i=0;i<s->in.n;i++) { const Sig *e=&s->in.v[i]; int k; if(_stricmp(e->name,"SV_VertexID")==0||_stricmp(e->name,"SV_InstanceID")==0) continue; if(stride+16>1024) { strcpy(out->label,"SKIP vertex stride"); goto done; }
      memset(&elems[offset],0,sizeof(elems[offset]));elems[offset].SemanticName=e->name;elems[offset].SemanticIndex=e->index;elems[offset].Format=format(e);elems[offset].InputSlot=0;elems[offset].AlignedByteOffset=stride;
      for(j=0;j<3;j++) for(k=0;k<comps(e->mask);k++) { float value=(!triangle_input && e->type==3 && comps(e->mask)==4)?(k==0?(j==1?3.0f:-1.0f):k==1?(j==2?-3.0f:1.0f):k==3?1.0f:0.0f):0.25f; unsigned char *at=vertices[j]+stride+k*4; if(e->type==3) memcpy(at,&value,4); else { UINT u=1;memcpy(at,&u,4); } }
      if(e->type==3 && comps(e->mask)==4)triangle_input=1;
      stride+=4*comps(e->mask);offset++;
    }
    if(offset) { hr=ID3D11Device_CreateInputLayout(d->dev,elems,offset,code,bytes,&layout);if(FAILED(hr)) goto fail; }
    if(stride) { unsigned char packed[3072]; memset(&bd,0,sizeof(bd));bd.ByteWidth=stride*3;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;for(i=0;i<3;i++)memcpy(packed+i*stride,vertices[i],stride);{D3D11_SUBRESOURCE_DATA init={packed,0,0};hr=ID3D11Device_CreateBuffer(d->dev,&bd,&init,&vb);}if(FAILED(hr))goto fail; }
  }
  for(i=0;i<CB_WORDS;i++)constants[i]=zero?0.0f:0.25f;
  for(i=0;i<14;i++) { D3D11_SUBRESOURCE_DATA init={constants,0,0};memset(&bd,0,sizeof(bd));bd.ByteWidth=sizeof(constants);bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;hr=ID3D11Device_CreateBuffer(d->dev,&bd,&init,&cb[i]);if(FAILED(hr))goto fail; }
  for(i=0;i<16;i++) {
    if(s->resource[i].dim) { hr=make_srv(d,s->resource[i],&srv[i]);if(FAILED(hr))goto fail; }
    memset(&sd,0,sizeof(sd));sd.Filter=s->comparison[i]?D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;sd.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;hr=ID3D11Device_CreateSamplerState(d->dev,&sd,&sam[i]);if(FAILED(hr))goto fail;
  }
  if(s->stage==0) { rt_count=0;for(i=0;i<s->out.n;i++)if(_stricmp(s->out.v[i].name,"SV_Target")==0 && s->out.v[i].reg<8 && s->out.v[i].reg+1>rt_count)rt_count=s->out.v[i].reg+1; if(!rt_count)rt_count=1; }
  memset(&td,0,sizeof(td));td.Width=td.Height=64;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
  for(i=0;i<rt_count;i++) { hr=ID3D11Device_CreateTexture2D(d->dev,&td,NULL,&rt[i]);if(FAILED(hr))goto fail;hr=ID3D11Device_CreateRenderTargetView(d->dev,(ID3D11Resource *)rt[i],NULL,&rtv[i]);if(FAILED(hr))goto fail; }
  if(s->depth) { td.Format=DXGI_FORMAT_D32_FLOAT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;hr=ID3D11Device_CreateTexture2D(d->dev,&td,NULL,&depth);if(FAILED(hr))goto fail;hr=ID3D11Device_CreateDepthStencilView(d->dev,(ID3D11Resource *)depth,NULL,&dsv);if(FAILED(hr))goto fail; }
  ID3D11DeviceContext_ClearState(d->ctx);for(i=0;i<rt_count;i++){const float black[4]={0,0,0,1};ID3D11DeviceContext_ClearRenderTargetView(d->ctx,rtv[i],black);}if(dsv)ID3D11DeviceContext_ClearDepthStencilView(d->ctx,dsv,D3D11_CLEAR_DEPTH,1.0f,0);
  ID3D11DeviceContext_OMSetRenderTargets(d->ctx,rt_count,rtv,dsv);memset(&viewport,0,sizeof(viewport));viewport.Width=viewport.Height=64;viewport.MaxDepth=1;ID3D11DeviceContext_RSSetViewports(d->ctx,1,&viewport);
  ID3D11DeviceContext_VSSetShader(d->ctx,vs,NULL,0);ID3D11DeviceContext_PSSetShader(d->ctx,ps,NULL,0);set_stage(d,s->stage,cb,srv,sam);ID3D11DeviceContext_IASetPrimitiveTopology(d->ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ID3D11DeviceContext_IASetInputLayout(d->ctx,layout);if(vb)ID3D11DeviceContext_IASetVertexBuffers(d->ctx,0,1,&vb,&stride,&(UINT){0});
  memset(&qd,0,sizeof(qd));qd.Query=D3D11_QUERY_EVENT;hr=ID3D11Device_CreateQuery(d->dev,&qd,&query);if(FAILED(hr))goto fail;
  ID3D11DeviceContext_Draw(d->ctx,3,0);ID3D11DeviceContext_End(d->ctx,(ID3D11Asynchronous *)query);ID3D11DeviceContext_Flush(d->ctx);
  for(;;) { BOOL complete=FALSE;hr=ID3D11DeviceContext_GetData(d->ctx,(ID3D11Asynchronous *)query,&complete,sizeof(complete),0);if(hr==S_OK && complete)break;if(FAILED(hr))goto fail;if(GetTickCount()-start>=3000){out->hang=1;strcpy(out->label,"HANG");goto done;}Sleep(1); }
  hr=ID3D11Device_GetDeviceRemovedReason(d->dev);if(FAILED(hr)){strcpy(out->label,"REMOVED");out->hang=1;goto done;}
  td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;hr=ID3D11Device_CreateTexture2D(d->dev,&td,NULL,&staging);if(FAILED(hr))goto fail;
  ID3D11DeviceContext_CopyResource(d->ctx,(ID3D11Resource *)staging,(ID3D11Resource *)rt[0]);hr=ID3D11DeviceContext_Map(d->ctx,(ID3D11Resource *)staging,0,D3D11_MAP_READ,0,&mapped);if(FAILED(hr))goto fail;
  out->pixel=rd((const unsigned char *)mapped.pData+32*mapped.RowPitch+32*4);for(i=0;i<16;i++)out->checksum=out->checksum*16777619u ^ rd((const unsigned char *)mapped.pData+32*mapped.RowPitch+(24+i)*4);ID3D11DeviceContext_Unmap(d->ctx,(ID3D11Resource *)staging,0);out->ok=1;strcpy(out->label,"OK");goto done;
fail: if(FAILED(hr)) { snprintf(out->label,sizeof(out->label),"CREATEFAIL_%08lx",(unsigned long)hr);if(d->dev && FAILED(ID3D11Device_GetDeviceRemovedReason(d->dev))){out->hang=1;strcpy(out->label,"REMOVED");} }
done: out->ms=GetTickCount()-start;REL(query);REL(staging);REL(dsv);REL(depth);for(i=0;i<8;i++){REL(rtv[i]);REL(rt[i]);}for(i=0;i<16;i++){REL(srv[i]);REL(sam[i]);}for(i=0;i<14;i++)REL(cb[i]);REL(vb);REL(layout);REL(vs);REL(ps);if(out->hang)close_device(d);
}
static int cmp_name(const void *a,const void *b) { return _stricmp(*(const char *const *)a,*(const char *const *)b); }
static unsigned char *load_file(const char *path,size_t *n) { HANDLE h=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);DWORD size,got;unsigned char *p;if(h==INVALID_HANDLE_VALUE)return NULL;size=GetFileSize(h,NULL);if(size==INVALID_FILE_SIZE||size>64*1024*1024){CloseHandle(h);return NULL;}p=(unsigned char *)malloc(size?size:1);if(!p){CloseHandle(h);return NULL;}if(!ReadFile(h,p,size,&got,NULL)||got!=size){free(p);p=NULL;}CloseHandle(h);*n=size;return p; }
int main(int argc,char **argv) { WIN32_FIND_DATAA fd; HANDLE find; char pattern[MAX_PATH],path[MAX_PATH];char **files=NULL;UINT nfiles=0,i,first=0,count=~0u,max_hangs=4,shaders=0,nir_hang=0,tgsi_hang=0,nir_only=0,diff=0,createfail=0;Device tgsi={0},nir={0};HANDLE watch;HMODULE dll;
  setvbuf(stdout,NULL,_IONBF,0);SetUnhandledExceptionFilter(crash);watch=CreateThread(NULL,0,watchdog,NULL,0,NULL);if(watch)CloseHandle(watch);
  if(argc<2){line("usage: agx_corpus_smoke.exe <corpus-dir> [first-index] [count] [max-hangs]");return 2;}if(argc>2)first=(UINT)strtoul(argv[2],NULL,10);if(argc>3)count=(UINT)strtoul(argv[3],NULL,10);if(argc>4)max_hangs=(UINT)strtoul(argv[4],NULL,10);
  dll=LoadLibraryA("d3dcompiler_47.dll");compiler=dll?(pD3DCompile)(void *)GetProcAddress(dll,"D3DCompile"):NULL;if(!compiler){line("NO_D3DCOMPILER");return 1;}
  if(snprintf(pattern,sizeof(pattern),"%s\\*.dxbc",argv[1])>=(int)sizeof(pattern))return 2;
  files=(char **)calloc(MAX_FILES,sizeof(char *));if(!files)return 1;find=FindFirstFileA(pattern,&fd);if(find!=INVALID_HANDLE_VALUE){do{if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&nfiles<MAX_FILES){size_t len=strlen(fd.cFileName)+1;files[nfiles]=(char *)malloc(len);if(files[nfiles]){memcpy(files[nfiles],fd.cFileName,len);nfiles++;}}}while(FindNextFileA(find,&fd));FindClose(find);}qsort(files,nfiles,sizeof(char *),cmp_name);
  for(i=first;i<nfiles && i-first<count;i++) { unsigned char *data;size_t len=0;Shader s;ID3DBlob *other=NULL;Run t[2],r[2];int k;const char *stage="?";char model[16];
    current_file=files[i];shader_started=(LONG)GetTickCount();if(snprintf(path,sizeof(path),"%s\\%s",argv[1],files[i])>=(int)sizeof(path)){line("CORPUS %u %s SKIP path",i,files[i]);shader_started=0;continue;}data=load_file(path,&len);if(!data||!parse(&s,data,len)){line("CORPUS %u %s SKIP invalid_dxbc",i,files[i]);free(data);shader_started=0;continue;}
    stage=s.stage==0?"PS":s.stage==1?"VS":s.stage==2?"GS":s.stage==3?"HS":s.stage==4?"DS":s.stage==5?"CS":"?";snprintf(model,sizeof(model),"%s_%d_%d",s.stage==0?"ps":s.stage==1?"vs":"sm",s.major,s.minor);
    if(s.stage>1||s.stage<0){line("CORPUS %u %s %s %s tgsi=SKIP_stage nir=SKIP_stage",i,files[i],stage,model);free(data);shader_started=0;continue;}
    other=companion(&s);if(!other){line("CORPUS %u %s %s %s tgsi=SKIP_companion nir=SKIP_companion",i,files[i],stage,model);free(data);shader_started=0;continue;}
    shaders++;for(k=0;k<2;k++){run(&tgsi,&s,other,0,k,&t[k]);run(&nir,&s,other,1,k,&r[k]);}
    line("CORPUS %u %s %s %s tgsi=%s/%s nir=%s/%s px=%08x/%08x checksum=%08x/%08x ms=%lu/%lu",i,files[i],stage,model,t[0].label,t[1].label,r[0].label,r[1].label,t[0].pixel,r[0].pixel,t[0].checksum,r[0].checksum,(unsigned long)(t[0].ms+t[1].ms),(unsigned long)(r[0].ms+r[1].ms));
    if(t[0].hang||t[1].hang)tgsi_hang++;if(r[0].hang||r[1].hang){nir_hang++;if(!t[0].hang&&!t[1].hang){nir_only++;line("NIR_ONLY_HANG %s",files[i]);}}
    for(k=0;k<2;k++){if(t[k].ok&&r[k].ok&&(t[k].pixel!=r[k].pixel||t[k].checksum!=r[k].checksum))diff++;if(strncmp(t[k].label,"CREATEFAIL",10)==0||strncmp(r[k].label,"CREATEFAIL",10)==0)createfail++;}
    ID3D10Blob_Release(other);free(data);shader_started=0;if(nir_hang>=max_hangs){line("STOP_MAX_HANGS");break;}
  }
  line("CORPUS_RESULT shaders=%u nir_hang=%u tgsi_hang=%u nir_only_hang=%u pixel_diff=%u createfail=%u",shaders,nir_hang,tgsi_hang,nir_only,diff,createfail);close_device(&tgsi);close_device(&nir);for(i=0;i<nfiles;i++)free(files[i]);free(files);FreeLibrary(dll);return 0;
}
