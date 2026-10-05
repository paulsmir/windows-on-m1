"""Executable native/Windows comparison of pinned fragment-epilog key layout."""
import argparse
import hashlib
from pathlib import Path
import subprocess

p=argparse.ArgumentParser()
p.add_argument('header',type=Path)
p.add_argument('output',type=Path)
p.add_argument('--cc',default='clang')
p.add_argument('--windows',action='store_true')
a=p.parse_args()
raw=a.header.read_bytes()
if hashlib.sha256(raw).hexdigest()!='bd469422d642fdbd136c978446426446d85b80070378e16fece842287136c6ff':
    raise SystemExit('Pinned agx_linker.h mismatch')
source=raw.decode()
begin=source.index('struct agx_fs_epilog_link_info {')
end=source.index('\n};',begin)+3
fragment=source[begin:end]
if a.windows:
    fragment=fragment.replace('struct agx_fs_epilog_link_info {',
        'struct __attribute__((aligned(4))) agx_fs_epilog_link_info {')
    # Keep the bool field boolean: assigning 2 must normalize to 1.
    fragment=fragment.replace('   unsigned ', '   uint8_t ')
fixture='''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
FRAGMENT
_Static_assert(sizeof(struct agx_fs_epilog_link_info)==4,"size");
_Static_assert(_Alignof(struct agx_fs_epilog_link_info)==4,"alignment");
_Static_assert(offsetof(struct agx_fs_epilog_link_info,loc_written)==2,"offset");
int main(void) {
  for(unsigned flags=0;flags<256;++flags) {
    struct agx_fs_epilog_link_info v;
    unsigned char expected[4]={0x12,0xA5,0x69,(unsigned char)flags};
    memset(&v,0,sizeof(v));
    v.rt_spill_base=0x12; v.size_32=0xA5; v.loc_written=0x69;
    v.sample_shading=(flags>>0)&1; v.broadcast_rt0=(flags>>1)&1;
    v.loc0_w_1=(flags>>2)&1; v.write_z=(flags>>3)&1;
    v.write_s=(flags>>4)&1; v.already_ran_zs=(flags>>5)&1;
    v.sample_mask_after_force_early=(flags&64)?2:0;
    v.padding=(flags>>7)&1;
    if(memcmp(&v,expected,4)) return 1;
  }
  puts("FS_EPILOG_LAYOUT: 256 combinations; sizeof=4 alignof=4 bool normalization PASS");
  return 0;
}
'''.replace('FRAGMENT',fragment)
a.output.mkdir(exist_ok=False)
c=a.output/'fixture.c'; c.write_text(fixture)
exe=a.output/('fixture.exe' if a.windows else 'fixture')
command=([a.cc,'/nologo','/std:c11','/W4','/WX',str(c),'/Fe:'+str(exe),'/Fo:'+str(a.output/'fixture.obj')]
         if a.windows else [a.cc,'-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)])
run=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(a.output/'build.log').write_bytes(run.stdout)
if run.returncode:
    print(run.stdout.decode(errors='replace')); raise SystemExit(run.returncode)
run=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(a.output/'test.log').write_bytes(run.stdout)
print(run.stdout.decode(errors='replace'))
raise SystemExit(run.returncode)
