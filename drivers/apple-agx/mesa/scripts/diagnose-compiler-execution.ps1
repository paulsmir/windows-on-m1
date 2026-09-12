$ErrorActionPreference='Stop'
$root='C:\Users\pauls\AD04-fullcompiler-001'
$attempt="$root\asahi-d25a261c91764f0597fae88dec4bf195"
$cdb='C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
$out=Join-Path $root ('execution-debug-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out | Out-Null
$commands='bu ucrtbase!fopen; g; da @rcx; gu; r rax; kp; q'
$args=@('-c',('"'+$commands+'"'),"$attempt\agx_shader_fixture.exe","$attempt\variant0",'0')
$p=Start-Process $cdb -ArgumentList $args -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\stdout.log" -RedirectStandardError "$out\stderr.log"
@{exit=$p.ExitCode;executable="$attempt\agx_shader_fixture.exe";arguments=$args}|ConvertTo-Json|Set-Content "$out\result.json"
Write-Output "DEBUG_EVIDENCE=$out"
Get-Content "$out\stdout.log" -Tail 65
