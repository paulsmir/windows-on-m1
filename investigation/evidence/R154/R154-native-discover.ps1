$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP867-r153-bm'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'dt -r2 AppleAgxRenderAdmission!_APPLE_AGX_PLATFORM_CHANNEL_BINDINGS 0xffff9b0acf051278; dt dxgkrnl!_DXGPROCESS; dt -r2 AppleAgxRenderAdmission!_APPLE_AGX_RENDER_MANAGER_STATE 0xffff9b0ad01c77a8; db 0xffff9b0acf052948 L38; q' 2>&1
