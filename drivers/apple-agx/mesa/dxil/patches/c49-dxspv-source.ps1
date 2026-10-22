$ErrorActionPreference='Stop'
$from='C:\Users\pauls\EXP1388-dxil-runtime-build-only\source'; $to='C:\Users\pauls\c49-dxspv-source'
if(Test-Path $to){throw 'exists'}
robocopy $from $to /E /NFL /NDL /NJH /NJS /XD .git | Out-Null
$f="$to\opcodes\dxil\dxil_arithmetic.cpp"; $t=[IO.File]::ReadAllText($f)
$o="`tspv::Id type_id = impl.get_type_id(instruction->getType());`n`tspv::Id quot_id = impl.spirv_module.get_helper_call_id(HelperCall::UDiv, type_id);"
$n="`t// The call returns dx.types.twoi32 {quotient, remainder}; the helpers`n`t// divide the operands' type.`n`tspv::Id type_id = impl.get_type_id(instruction->getOperand(1)->getType());`n`tspv::Id quot_id = impl.spirv_module.get_helper_call_id(HelperCall::UDiv, type_id);"
$c=([regex]::Matches($t,[regex]::Escape($o.Replace("`n","`r`n")))).Count; $nl="`r`n"
if($c -ne 1){ $c=([regex]::Matches($t,[regex]::Escape($o))).Count; $nl="`n" }
if($c -ne 1){throw "PATCH_ANCHOR $c"}
$t=$t.Replace($o.Replace("`n",$nl),$n.Replace("`n",$nl)); [IO.File]::WriteAllText($f,$t)
"PATCHED $((Get-FileHash $f).Hash)"
