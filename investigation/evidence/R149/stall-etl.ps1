$ErrorActionPreference='Stop'
$base='C:\agx\r149-stall'
$counts=@{}
$examples=@{}
$w=[System.IO.StreamWriter]::new("$base\events.jsonl",$false,[System.Text.UTF8Encoding]::new($false))
try {
Get-WinEvent -Path "$base\EXP862.etl" -Oldest | ForEach-Object {
 $e=$_; $key="$($e.ProviderName)/$($e.Id)/$($e.Version)/$($e.Opcode)"
 if(!$counts.ContainsKey($key)) { $counts[$key]=0; $examples[$key]=$e.ToXml() }; $counts[$key]++
 $x=[xml]$e.ToXml(); $data=[ordered]@{}
 foreach($d in $x.Event.EventData.Data) { $data[$d.Name]=$d.'#text' }
 $o=[ordered]@{t=$x.Event.System.TimeCreated.SystemTime;provider=$e.ProviderName;id=$e.Id;version=$e.Version;op=$e.Opcode;pid=$e.ProcessId;tid=$e.ThreadId;data=$data}
 $w.WriteLine(($o|ConvertTo-Json -Compress -Depth 8))
}
} finally { $w.Dispose() }
$counts|ConvertTo-Json|Set-Content "$base\event-counts.json"
$examples|ConvertTo-Json -Depth 5|Set-Content "$base\event-examples.json"
Get-Item "$base\events.jsonl" | Select-Object Length
