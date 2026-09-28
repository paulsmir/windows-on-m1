$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP867-r153-bm'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c '.echo PARENTS; .for (r @$t0=0xffff9b0ad02bd9e0; @$t0 != 0; r @$t0=poi(@$t0)) { dq @$t0 L9; }; .echo LEAVES; .for (r @$t0=0xffff9b0acf6e1a00; @$t0 != 0; r @$t0=poi(@$t0)) { dq @$t0 L9; }; .echo SHADOWS; .for (r @$t0=0xffff9b0aca870560; @$t0 != 0; r @$t0=poi(@$t0)) { dq @$t0 L12; }; .echo COMMAND; db 0xffff9b0acef7e988 L118; q' 2>&1
