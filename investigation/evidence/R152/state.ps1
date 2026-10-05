$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP866-r151-stamp'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'dt -r2 AppleAgxRenderAdmission!_APPLE_AGX_PLATFORM_PROVIDER ffffe20519fec228; dt -r2 AppleAgxRenderAdmission!_ADMISSION_G3_PROCESS ffffe20519be67e0; dt -r2 AppleAgxRenderAdmission!_APPLE_AGX_RTKIT_SESSION ffffe20519fe45c0; dt -r2 AppleAgxRenderAdmission!_ADMISSION_BACKEND_IMAGE ffffe20519d7dd40; q' 2>&1
