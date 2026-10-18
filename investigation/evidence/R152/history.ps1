$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP866-r151-stamp'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'dd ffffcc03f51a4000 Lc; dd ffffcc03f51ac000 Lc; dd ffffcc03f5260000 Lc; db ffffcc03f5268000 L1f8; db ffffcc03f5208000 L140; db ffffcc03f5210000 L140; dt -r2 AppleAgxRenderAdmission!_APPLE_AGX_RENDER_SHARED_MEMORY ffffe20519fec228+440; dl ffffe2051a051040 10 2; q' 2>&1
