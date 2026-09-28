$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP866-r151-stamp'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c '.for (r @$t0=poi(ffffe20519be67e0+18+70); @$t0 != 0; r @$t0=poi(@$t0)) { .if (dwo(@$t0+20)==8) { dt AppleAgxRenderAdmission!_APPLE_AGX_GPUVA_G3_NODE @$t0 Ipa AuxIpa Index Kind; }; .if (dwo(@$t0+20)==c) { dt AppleAgxRenderAdmission!_APPLE_AGX_GPUVA_G3_NODE @$t0 Ipa AuxIpa Index Kind; }; .if (dwo(@$t0+20)==134) { dt AppleAgxRenderAdmission!_APPLE_AGX_GPUVA_G3_NODE @$t0 Ipa AuxIpa Index Kind; }; .if (dwo(@$t0+20)==138) { dt AppleAgxRenderAdmission!_APPLE_AGX_GPUVA_G3_NODE @$t0 Ipa AuxIpa Index Kind; }; }; q' 2>&1
