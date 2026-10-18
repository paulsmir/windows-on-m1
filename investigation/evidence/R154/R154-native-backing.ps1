$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP867-r153-bm'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'dt -r2 AppleAgxRenderAdmission!_ADMISSION_G3_PRIVATE_SCENE 0xffff9b0acec312e0; dx -r3 ((AppleAgxRenderAdmission!_ADMISSION_CONTEXT*)0xffff9b0acef02000)->BackendImage.G4Header; dt AppleAgxRenderAdmission!_APPLE_AGX_GPUVA_G3_NODE; dd 0xffff9b0ace26b6f8 L74; dd 0xffff9b0ad01c77a8 L74; dx -r2 ((AppleAgxRenderAdmission!_ADMISSION_PLATFORM_RUNTIME*)0xffff9b0acf049000)->QueueObjects[1]; dx -r2 ((AppleAgxRenderAdmission!_ADMISSION_PLATFORM_RUNTIME*)0xffff9b0acf049000)->QueueObjects[13]; q' 2>&1
