$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP866-r151-stamp'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'dt -r2 AppleAgxRenderAdmission!_APPLE_AGX_RENDER_SHARED_MEMORY_OWNER ffffe20519fe7800; dx -r2 ((AppleAgxRenderAdmission!_APPLE_AGX_RENDER_SHARED_MEMORY_OWNER*)0xffffe20519fe7800)->Objects; dx -r2 ((AppleAgxRenderAdmission!_APPLE_AGX_PLATFORM_PROVIDER*)0xffffe20519fec228)->QueueRelocationObjects; dt -r2 AppleAgxRenderAdmission!_ADMISSION_G3_PRIVATE_SCENE ffffe2051b4d8db0; dt AppleAgxRenderAdmission!APPLE_AGX_G4_NATIVE_RENDER ffffe20519d7dd40+c48+28; q' 2>&1
