$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP867-r153-bm'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'dd 0xffffab0f1e060000 L20; .for (r @$t0=0; @$t0 < 0xa2; r @$t0=@$t0+1) { .printf "EV %x %08x %08x %08x %08x %08x\n", @$t0, dwo(0xffffab0f1e068000+@$t0*0x38),dwo(0xffffab0f1e068004+@$t0*0x38),dwo(0xffffab0f1e068008+@$t0*0x38),dwo(0xffffab0f1e06800c+@$t0*0x38),dwo(0xffffab0f1e068010+@$t0*0x38); }; dt -r1 AppleAgxRenderAdmission!_APPLE_AGX_RENDER_SHARED_MEMORY_OWNER 0xffff9b0acf04c800; dq 0xffff9b0acab987a0 L30; !process 0 0; q' 2>&1
