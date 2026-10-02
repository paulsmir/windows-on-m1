param([Parameter(Mandatory=$true)][string]$Exe)
$ErrorActionPreference='Stop'
& $Exe --self-test
if($LASTEXITCODE){throw 'identity test failed'}
& $Exe --alias-self-test
if($LASTEXITCODE){throw 'alias test failed'}
$dir=Join-Path ([IO.Path]::GetTempPath()) ('EXP931-compare-'+[Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($dir)|Out-Null
try {
 $a=Join-Path $dir 'a.bin';$b=Join-Path $dir 'b.bin'
 $data=New-Object byte[] 20001
 for($i=0;$i -lt $data.Length;$i++){$data[$i]=[byte]($i%251)}
 [IO.File]::WriteAllBytes($a,$data);[IO.File]::WriteAllBytes($b,$data)
 & $Exe --compare-files $a $b
 if($LASTEXITCODE -ne 0){throw 'identical multi-block files rejected'}
 $data[10000]=$data[10000] -bxor 1;[IO.File]::WriteAllBytes($b,$data)
 & $Exe --compare-files $a $b
 if($LASTEXITCODE -ne 33){throw 'same-length corruption accepted'}
 [IO.File]::WriteAllBytes($b,$data[0..8191])
 & $Exe --compare-files $a $b
 if($LASTEXITCODE -ne 33){throw 'truncated backup accepted'}
 & $Exe --compare-files $a (Join-Path $dir 'absent.bin')
 if($LASTEXITCODE -ne 31){throw 'missing backup accepted'}
 $locked=[IO.File]::Open($b,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 try {& $Exe --compare-files $a $b;$code=$LASTEXITCODE} finally {$locked.Dispose()}
 if($code -ne 31){throw 'unreadable backup accepted'}
 'BINARY_BACKUP_EQUAL_MISMATCH_TRUNCATION_MISSING_LOCKED_PASS'
} finally {Remove-Item $dir -Recurse -Force}
