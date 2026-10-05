$ProgressPreference='SilentlyContinue'
$ErrorActionPreference='Stop'
$base='C:\Users\pauls\R148-memory'
$counts=@{}; $examples=@{}
$w=[System.IO.StreamWriter]::new("$base\lifetime-events.jsonl",$false,[System.Text.UTF8Encoding]::new($false))
try {
Get-WinEvent -FilterHashtable @{Path="$base\EXP801DxgBoot.etl";Id=@(27,28,33,34,36,37)} -Oldest | ForEach-Object {
 $e=$_; $key="$($e.ProviderName)/$($e.Id)/$($e.Version)/$($e.Opcode)"
 if(!$counts.ContainsKey($key)) { $counts[$key]=0; $examples[$key]=$e.ToXml() }; $counts[$key]++
 if ($e.Id -in @(0,1,2,3,27,28,33,34,36,37,39,40,41,42,80,182,216,225,226,288,289,320,338,339)) {
 $x=[xml]$e.ToXml(); $data=[ordered]@{}
 foreach($d in $x.Event.EventData.Data) { $data[$d.Name]=$d.'#text' }
 $o=[ordered]@{t=$x.Event.System.TimeCreated.SystemTime;provider=$e.ProviderName;id=$e.Id;version=$e.Version;op=$e.Opcode;pid=$e.ProcessId;tid=$e.ThreadId;data=$data}
 $w.WriteLine(($o|ConvertTo-Json -Compress -Depth 8))
 }
}
} finally { $w.Dispose(); $counts|ConvertTo-Json|Set-Content "$base\lifetime-counts.json"; $examples|ConvertTo-Json -Depth 5|Set-Content "$base\lifetime-examples.json" }
Get-Item "$base\lifetime-events.jsonl" | Select-Object Length
