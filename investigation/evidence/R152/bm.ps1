$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP866-r151-stamp'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'dx -r1 ((AppleAgxRenderAdmission!_APPLE_AGX_RENDER_SHARED_MEMORY_OWNER*)0xffffe20519fe7800)->ObjectOffsets; db ffffcc03f5358000+3f44 Lbc; dt -r2 AppleAgxRenderAdmission!_APPLE_AGX_PLATFORM_PROVIDER ffffe20519fec228 QueueObjects; dt -r2 AppleAgxRenderAdmission!_ADMISSION_G3_PROCESS ffffe2051952add0; dt -r2 AppleAgxRenderAdmission!_ADMISSION_G3_PROCESS ffffe20519a97010; q' 2>&1
