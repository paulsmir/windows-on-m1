$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP867-r153-bm'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'dd 0xffffab0f1e068000 L8dc; dt -r1 AppleAgxRenderAdmission!_APPLE_AGX_RENDER_SHARED_MEMORY_OWNER 0xffff9b0acf04c800; dq 0xffff9b0acab987a0 L30; dq 0xffff9b0ace5199c0 L30; !process 0 0; q' 2>&1
