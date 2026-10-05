$ErrorActionPreference='Stop'
$b='C:\Users\pauls\EXP867-r153-bm'
$tool='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
& $tool -y "srv*C:\Symbols*https://msdl.microsoft.com/download/symbols;$b" -z "$b\Kernel-MEMORY.DMP" -c 'dq 0xffffab0f1e18b800 L7d; dq 0xffffab0f1e174800 La2; dd 0xffffab0f1e15cf44 L2f; dd 0xffffab0f1e1bbfb8 L12; dx -r3 ((AppleAgxRenderAdmission!_ADMISSION_G3_PROCESS*)0xffff9b0ace26b520)->PrivateManager; dx -r3 ((AppleAgxRenderAdmission!_ADMISSION_G3_PRIVATE_SCENE*)0xffff9b0acec312e0)->Storage; q' 2>&1
