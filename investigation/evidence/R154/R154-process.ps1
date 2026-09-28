$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP867-r153-bm'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'dt -r1 AppleAgxRenderAdmission!_ADMISSION_G3_PROCESS ffff9b0ace26b520; dt -r1 AppleAgxRenderAdmission!_ADMISSION_G3_STATE ffff9b0acf1aa000; dx -r2 ((AppleAgxRenderAdmission!_ADMISSION_PLATFORM_RUNTIME*)0xffff9b0acf049000)->Rtkit; dx -r2 ((AppleAgxRenderAdmission!_ADMISSION_CONTEXT*)0xffff9b0acef02000)->RenderCorrelation; q' 2>&1
