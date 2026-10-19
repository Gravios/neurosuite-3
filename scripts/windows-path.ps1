<#
.SYNOPSIS
  Puts the installed neurosuite-3 programs on the user's PATH, or takes them off.

.DESCRIPTION
  `cmake --install` runs this with -Add <prefix>\bin (-DNS_ADD_TO_PATH=OFF to
  skip).  It edits only the current user's PATH (HKCU\Environment; no
  administrator rights), keeps the value's type (REG_EXPAND_SZ) and its
  %VARIABLES% unexpanded, and records the directory in NEUROSUITE3_BIN, so a
  later install to another prefix replaces the old entry instead of adding a
  second one.  Prompts and programs opened afterwards see the change; ones
  already open keep their old PATH.

    powershell -ExecutionPolicy Bypass -File scripts\windows-path.ps1 -Add C:\ns3\bin
    powershell -ExecutionPolicy Bypass -File scripts\windows-path.ps1 -Remove
    powershell -ExecutionPolicy Bypass -File scripts\windows-path.ps1 -Show

  ASCII only and Windows PowerShell 5.1 syntax.

SPDX-License-Identifier: GPL-3.0-or-later
#>
[CmdletBinding(DefaultParameterSetName = 'Show')]
param(
    [Parameter(ParameterSetName = 'Add', Mandatory = $true)][string]$Add,
    [Parameter(ParameterSetName = 'Remove', Mandatory = $true)][switch]$Remove,
    [Parameter(ParameterSetName = 'Show')][switch]$Show
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$MarkerName = 'NEUROSUITE3_BIN'

# Same directory?  Case-insensitive, ignoring a trailing separator and / vs \.
function Get-PathKey([string]$dir) {
    return ($dir -replace '/', '\').TrimEnd('\').ToLowerInvariant()
}

# The PATH entries without $drop (any spelling of it), in their order.
function Remove-PathEntry([string[]]$entries, [string]$drop) {
    if (-not $drop) { return ,$entries }
    $key = Get-PathKey $drop
    return ,@($entries | Where-Object { $_ -and ((Get-PathKey $_) -ne $key) })
}

# PATH with $previous (an earlier install elsewhere) taken out and $dir
# appended, unless it is already there (then it keeps its place).  An
# unchanged PATH comes back as the same string.
function Get-NewPath([string]$path, [string]$dir, [string]$previous) {
    $entries = @()
    if ($path) { $entries = @($path -split ';' | Where-Object { $_ }) }
    $changed = $false
    if ($previous -and ((Get-PathKey $previous) -ne (Get-PathKey $dir))) {
        $kept = Remove-PathEntry $entries $previous
        if ($kept.Count -ne $entries.Count) { $changed = $true }
        $entries = $kept
    }
    if ($dir) {
        $key = Get-PathKey $dir
        $present = @($entries | Where-Object { (Get-PathKey $_) -eq $key }).Count -gt 0
        if (-not $present) { $entries += $dir; $changed = $true }
    }
    if (-not $changed) { return $path }
    return ($entries -join ';')
}

if ($env:NS_WINDOWS_PATH_SELFTEST) { return }   # lets a test dot-source the functions

$key = Get-Item -Path 'HKCU:\Environment'
# Raw value: %USERPROFILE% and friends stay unexpanded.
$path = $key.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
$previous = $key.GetValue($MarkerName, '')

switch ($PSCmdlet.ParameterSetName) {
    'Show' {
        if ($previous) { Write-Host "neurosuite-3 programs on the user PATH: $previous" }
        else { Write-Host 'neurosuite-3 programs are not on the user PATH (no NEUROSUITE3_BIN).' }
        return
    }
    'Add' {
        $dir = $Add -replace '/', '\'
        $new = Get-NewPath $path $dir $previous
        $verb = 'added to'
        if ($new -eq $path) { $verb = 'already on' }
    }
    'Remove' {
        if (-not $previous) { Write-Host 'neurosuite-3: nothing to remove (no NEUROSUITE3_BIN).'; return }
        $dir = $previous
        $new = Get-NewPath $path '' $previous
        $verb = 'removed from'
    }
}

if ($new -ne $path) {
    New-ItemProperty -Path 'HKCU:\Environment' -Name 'Path' -Value $new `
        -PropertyType ExpandString -Force | Out-Null
}
# Setting a variable through .NET also broadcasts WM_SETTINGCHANGE, so Explorer
# and newly opened prompts pick up the new PATH without signing out.
if ($PSCmdlet.ParameterSetName -eq 'Add') {
    [Environment]::SetEnvironmentVariable($MarkerName, $dir, 'User')
} else {
    [Environment]::SetEnvironmentVariable($MarkerName, $null, 'User')
}
Write-Host "neurosuite-3: $dir $verb the user PATH (open a new prompt to use it)"
