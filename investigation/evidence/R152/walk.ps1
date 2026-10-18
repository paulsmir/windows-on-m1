$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP866-r151-stamp'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'dd ffffcc03f556ffa0 L18; dd ffffcc03f5567fa0 L18; .for (r @$t0=poi(ffffe20519be67e0+18+68); @$t0 != 0; r @$t0=poi(@$t0)) { dt AppleAgxRenderAdmission!_APPLE_AGX_GPUVA_G3_NODE @$t0 Ipa AuxIpa Index; }; .for (r @$t0=poi(ffffe20519be67e0+18+70); @$t0 != 0; r @$t0=poi(@$t0)) { .if ((dwo(@$t0+20)>=8 && dwo(@$t0+20)<10) || (dwo(@$t0+20)>=134 && dwo(@$t0+20)<13c)) { dt AppleAgxRenderAdmission!_APPLE_AGX_GPUVA_G3_NODE @$t0 Ipa AuxIpa Index Kind; } }; q' 2>&1
