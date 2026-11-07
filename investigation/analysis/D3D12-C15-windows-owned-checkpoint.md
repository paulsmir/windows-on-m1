# Original owned DXIL adapter: Windows ARM64 build checkpoint

EXP1399 stopped before compilation on an experimental path guard. A native
FileInfo.Directory returns System.IO.DirectoryInfo without PowerShell provider
PSIsContainer. The walker therefore selected the file branch again and lost its
parent. Original1399 source, manifest, inputs and raw failure were preserved.

EXP1402 changes only that walker branch to `-is [IO.DirectoryInfo]`, plus the
new experiment label/root. AST-extracted actual original/fixed Under functions
prove original nested failure and fixed nested/direct success, three outside-root
refusals and rejection of a real NTFS junction. Prefix/reparse checks remain.
The same latent error was corrected in the unexecuted1400 Safe preparation,
with original versions backed up before its first execution.

The three original C11 units (parser, owned adapter, mixed caller) compiled and
linked under MSVC14.44.35207/SDK26100 ARM64 with /MD and the nine unchanged
EXP1395 static libraries. No translator rebuild, new external source or driver
build/capability change. Parser/adapter logs contain no warnings; fixture has
two C4996 fopen deprecation warnings, with /W4 and no /WX or suppressions.
ARM64 PE, CRT directives/import allowlist, input/tool and notices gates pass.
Imports are MSVCP140/VCRUNTIME140/UCRT API sets/KERNEL32, without AGX/D3D imports.

All47 original artifact-manifest entries and all21 distribution files match
size/SHA256. Parent also verified all52 collected final-host-manifest files.
Distribution contains the1090048-byte EXE, original3704-byte fixture,16notice/
index/combined files and three raw licenses. The executable was NOT run by1402.

Evidence under /Users/pavel/public_windows/.local/c15-owned-win-1402/:

- zip ad466259bb1170df9f7aca5987f80ee63e2b77d8ea5118e551460510f9321ebe
- build script8db8a4b2a60e4e1c30b5ae14ee9356567b4b49dcd559868b035dd99a7642aa81
- AST regression5e323991e5d3323a2dced8e445adb65404aadc119b47a82440088b45e327d9f8
- regression resultb9a6acbc6e126e3423e663a86d868f346b4cb1591136128d1d9d214e74830833
- final-host-manifest dbab8c3c48ae972eb141745c07d53501a8237d33ae434c20e38a1e9b4fab791d
- original artifact manifest eb5481ba376c60b900b6b7c59644abb5cb1f9802a5319e175ba4424e2baf1e8d
- distribution manifest68d5b495da3af508759a7513d38363fa62a2748e18305195ae2f024921a363e0
- EXE3fffd41cc125de1118181f4319704c21f7f61d75b59fbda7348c14da24c77a1e

The next CPU-only1400 run must have its own BEFORE, exact uploaded hashes,
Code28/package-absent parent gates and completed1390 recovery. It tests actual
Windows ARM64 C API/CRT/TLS and limit-refusal/valid-again behavior. It does not
exercise GPU/DDI/owner/model admission or establish recoverable internal OOM.
