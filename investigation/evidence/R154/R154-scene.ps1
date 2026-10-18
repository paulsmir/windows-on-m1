$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP867-r153-bm'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'dt -r2 AppleAgxRenderAdmission!_ADMISSION_G3_PRIVATE_SCENE ffff9b0acec312e0; dx -r3 ((AppleAgxRenderAdmission!_ADMISSION_CONTEXT*)0xffff9b0acef02000)->RenderCorrelation; q' 2>&1
