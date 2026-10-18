$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP867-r153-bm'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'dt -r1 AppleAgxRenderAdmission!_ADMISSION_RENDER_CONTEXT ffff9b0acb092170; dx -r2 ((AppleAgxRenderAdmission!_ADMISSION_CONTEXT*)0xffff9b0acef02000)->G4SubmitFailure; dx -r2 ((AppleAgxRenderAdmission!_ADMISSION_CONTEXT*)0xffff9b0acef02000)->BackendImage; q' 2>&1
