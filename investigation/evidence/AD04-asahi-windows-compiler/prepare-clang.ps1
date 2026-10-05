$ErrorActionPreference='Stop'
$root='C:\Users\pauls\AD04-asahi-windows-compiler'
$archive=Join-Path $root 'LLVM-20.1.8-win64.exe'
$sha='3197846a2b19063687dd56e93e34cd941e3548d907f23a6131571321bdf9fe7b'
if(!(Test-Path $archive)) {
 & curl.exe --fail --location --max-time 240 --output $archive 'https://github.com/llvm/llvm-project/releases/download/llvmorg-20.1.8/LLVM-20.1.8-win64.exe'
 if($LASTEXITCODE){throw 'LLVM download failed'}
}
if((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sha){throw 'LLVM SHA256 mismatch'}
$out=Join-Path $root 'llvm20'
if(!(Test-Path "$out\bin\clang-cl.exe")) {
 & 'C:\Program Files\7-Zip\7z.exe' x $archive "-o$out" -y *> (Join-Path $root 'llvm-extract.log')
 if($LASTEXITCODE){throw 'LLVM extraction failed'}
}
& "$out\bin\clang-cl.exe" --version
if($LASTEXITCODE){throw 'Clang self check failed'}
