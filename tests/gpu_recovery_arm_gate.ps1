# Execute only the actual helper's AST-extracted arm gate against read-only
# registry mocks. Never dot-source the helper's top level or contact a service.
[CmdletBinding()]
param([string]$HelperPath = (Join-Path $PSScriptRoot '..\scripts\agx-recovery-disable-service.ps1'))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Resolve-Path -LiteralPath $HelperPath).Path, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count -ne 0) { throw "helper parse errors: $parseErrors" }
$functions = @($ast.FindAll({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Assert-Disarmed'
}, $true))
if ($functions.Count -ne 1) { throw 'expected exactly one actual arm gate' }
. ([scriptblock]::Create($functions[0].Extent.Text))

# Keep the native registry API's method names and value-kind semantics.
# Every failure mode throws rather than silently returning a null value.
class ArmRegistryMock {
    [hashtable]$Values = @{}
    [hashtable]$Kinds = @{}
    [string]$Failure = ''
    [string[]] GetValueNames() {
        if ($this.Failure -eq 'names') { throw 'enumeration denied' }
        return [string[]]@($this.Values.Keys)
    }
    [Microsoft.Win32.RegistryValueKind] GetValueKind([string]$name) {
        if ($this.Failure -eq 'kind') { throw 'type read denied' }
        return [Microsoft.Win32.RegistryValueKind]$this.Kinds[$name]
    }
    [object] GetValue([string]$name) {
        if ($this.Failure -eq 'value') { throw 'value read denied' }
        return $this.Values[$name]
    }
}
$deviceKey = 'Mock:\GpuParameters'
$script:key = [ArmRegistryMock]::new()
$script:openFails = $false
function Get-Item {
    param([string]$LiteralPath)
    if ($LiteralPath -ne $deviceKey) { throw 'unexpected registry path' }
    if ($script:openFails) { throw 'key open denied' }
    return $script:key
}
function Expect-Refusal([string]$Case) {
    $refused = $false
    try { $null = Assert-Disarmed } catch { $refused = $true }
    if (!$refused) { throw "unsafe arm state accepted: $Case" }
    Write-Output "ARM_GATE $Case REFUSED"
}

$state = Assert-Disarmed
if ($state.G3Armed.Present -or $state.B1Armed.Present -or
    $null -ne $state.G3Armed.Value -or $state.G3Armed.State -ne 'Absent' -or
    $state.B1Armed.State -ne 'Absent') { throw 'absence misreported' }
Write-Output 'ARM_GATE absent ALLOWED truthful-absence'
foreach ($name in @('G3Armed', 'B1Armed')) {
    $script:key.Values[$name] = 0
    $script:key.Kinds[$name] = [Microsoft.Win32.RegistryValueKind]::DWord
}
$state = Assert-Disarmed
if (!$state.G3Armed.Present -or !$state.B1Armed.Present -or
    $state.G3Armed.Value -ne 0 -or $state.B1Armed.State -ne 'ExplicitZero') { throw 'zero misreported' }
Write-Output 'ARM_GATE DWORD-zero ALLOWED truthful-presence'
$script:key.Values.Remove('B1Armed')
$state = Assert-Disarmed
if (!$state.G3Armed.Present -or $state.B1Armed.Present) { throw 'mixed absence/zero misreported' }
Write-Output 'ARM_GATE mixed-zero-absent ALLOWED'
foreach ($name in @('G3Armed', 'B1Armed')) {
    $script:key.Values[$name] = 1
    $script:key.Kinds[$name] = [Microsoft.Win32.RegistryValueKind]::DWord
    Expect-Refusal "$name-one"
    $script:key.Values[$name] = 0
    $script:key.Kinds[$name] = [Microsoft.Win32.RegistryValueKind]::String
    Expect-Refusal "$name-wrong-type"
    $script:key.Kinds[$name] = [Microsoft.Win32.RegistryValueKind]::DWord
}
foreach ($failure in @('names', 'kind', 'value')) {
    $script:key.Failure = $failure
    Expect-Refusal "$failure-read-error"
}
$script:key.Failure = ''
$script:openFails = $true
Expect-Refusal 'key-open-error'
Write-Output 'AGX_RECOVERY_ARM_GATE_TEST: PASS'
