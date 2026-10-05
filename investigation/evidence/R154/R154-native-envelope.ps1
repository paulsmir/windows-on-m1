$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP867-r153-bm'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'dt AppleAgxRenderAdmission!_APPLE_AGX_GPUVA_G3_LOGICAL_PTE 0xffff9b0ad2574a00; dd 0xffff9b0acef7e8e0 L2a; q' 2>&1
