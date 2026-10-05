$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP866-r151-stamp'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'db ffffe20519fec228+16d0 L38; dt -r3 AppleAgxRenderAdmission!_APPLE_AGX_PLATFORM_CHANNEL_BINDINGS ffffe20519fec228; dq ffffcc03f5389800 L6; dq ffffcc03f5371800 La; dt -r1 AppleAgxRenderAdmission!_ADMISSION_G3_PROCESS ffffe20519be67e0; q' 2>&1
