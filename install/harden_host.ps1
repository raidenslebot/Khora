<#
  harden_host.ps1 — one-time HOST hardening for running Khora contained.

  WHY THIS EXISTS
  Khora's per-command cage (the Bulwark: low-integrity non-admin token + Job Object)
  contains every command Khora runs autonomously. But a red-team audit of THIS machine
  found host-level facts the per-command cage cannot fix by itself:
    - the operator account is a split-token admin, and
    - UAC is set to ConsentPromptBehaviorAdmin = 0 (elevate WITHOUT a prompt),
  which means a process could obtain full admin with no consent dialog. And a GPU hang
  from exploration can black-screen the desktop unless TDR recovery is on.

  This script is AUDIT-FIRST: run with no arguments it CHANGES NOTHING and just reports
  your real security posture. -Apply makes only the safe, reversible changes. -Revert
  undoes them. Nothing here is theatrical; every line is a concrete Win32/registry fact.

  USAGE (from an ELEVATED PowerShell):
    .\harden_host.ps1                 # audit only — reports posture, changes nothing
    .\harden_host.ps1 -Apply          # apply TDR recovery (safe default)
    .\harden_host.ps1 -Apply -HardenUAC   # also require a real UAC prompt (stops silent elevation)
    .\harden_host.ps1 -Revert         # undo what -Apply set

  NOT YET AUTOMATED (the stronger tiers, documented honestly):
    - a dedicated low-privilege local user 'khora_sbx' to RUN khora.exe as, with an NTFS
      disk quota and DENY-write ACLs on src/include/build so even a self-rewrite cannot
      escape; a VHDX-isolated cell; and a SYSTEM 'Warden' service that reaps any scheduled
      task / service / Run-key Khora creates. Those need deliberate setup and are the next
      containment tier, not this script.
#>

[CmdletBinding()]
param(
    [switch]$Apply,
    [switch]$Revert,
    [switch]$HardenUAC
)

$ErrorActionPreference = 'Stop'
$uacKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System'
$gfxKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\GraphicsDrivers'

function Test-Elevated {
    $id = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object System.Security.Principal.WindowsPrincipal($id)).IsInRole(
        [System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-RegValue($path, $name) {
    try { (Get-ItemProperty -Path $path -Name $name -ErrorAction Stop).$name } catch { $null }
}

Write-Host "=== Khora host posture (audit) ===" -ForegroundColor Cyan
$elevated = Test-Elevated
Write-Host ("  running elevated            : {0}" -f $elevated)
$consent = Get-RegValue $uacKey 'ConsentPromptBehaviorAdmin'
$secure  = Get-RegValue $uacKey 'PromptOnSecureDesktop'
$tdr     = Get-RegValue $gfxKey 'TdrLevel'
Write-Host ("  ConsentPromptBehaviorAdmin  : {0}   (0 = SILENT elevation = risk; 2 = prompt on secure desktop)" -f $consent)
Write-Host ("  PromptOnSecureDesktop       : {0}" -f $secure)
Write-Host ("  GPU TdrLevel                : {0}   (3 = recover on hang = desired; blank = default 3)" -f $tdr)
$sys = Get-PSDrive -Name ($env:SystemDrive.TrimEnd(':')) -ErrorAction SilentlyContinue
if ($sys) {
    $freeGB = [math]::Round($sys.Free / 1GB, 1)
    Write-Host ("  {0} free space             : {1} GB   (a full system drive can hang/brick boot)" -f $env:SystemDrive, $freeGB)
}
Write-Host ""

if (-not $Apply -and -not $Revert) {
    Write-Host "Audit only. Re-run with -Apply (optionally -HardenUAC) from an elevated prompt to change anything." -ForegroundColor Yellow
    return
}

if (-not $elevated) {
    throw "This must run from an ELEVATED PowerShell to change HKLM. Right-click PowerShell -> Run as administrator."
}

if ($Apply) {
    Write-Host "Applying safe hardening..." -ForegroundColor Green
    # GPU TDR recovery: turn a Khora-induced GPU hang into a ~2s driver reset, not a freeze.
    New-ItemProperty -Path $gfxKey -Name 'TdrLevel' -Value 3 -PropertyType DWord -Force | Out-Null
    Write-Host "  [set] TdrLevel = 3 (GPU hang recovers instead of freezing)"

    if ($HardenUAC) {
        # Require a real prompt on the secure desktop for any elevation. This stops a
        # process from silently becoming admin. You WILL see UAC prompts after this.
        New-ItemProperty -Path $uacKey -Name 'ConsentPromptBehaviorAdmin' -Value 2 -PropertyType DWord -Force | Out-Null
        New-ItemProperty -Path $uacKey -Name 'PromptOnSecureDesktop'      -Value 1 -PropertyType DWord -Force | Out-Null
        Write-Host "  [set] ConsentPromptBehaviorAdmin = 2, PromptOnSecureDesktop = 1 (silent elevation OFF)"
        Write-Host "        NOTE: you will now get UAC prompts for elevation. That is the point." -ForegroundColor Yellow
    } else {
        Write-Host "  [skip] UAC hardening (pass -HardenUAC to require a real elevation prompt)" -ForegroundColor Yellow
    }
    Write-Host "Done. Re-run with no args to confirm the new posture." -ForegroundColor Green
}

if ($Revert) {
    Write-Host "Reverting..." -ForegroundColor Green
    New-ItemProperty -Path $gfxKey -Name 'TdrLevel' -Value 3 -PropertyType DWord -Force | Out-Null  # 3 is the safe default anyway
    New-ItemProperty -Path $uacKey -Name 'ConsentPromptBehaviorAdmin' -Value 0 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $uacKey -Name 'PromptOnSecureDesktop'      -Value 0 -PropertyType DWord -Force | Out-Null
    Write-Host "  Restored ConsentPromptBehaviorAdmin = 0, PromptOnSecureDesktop = 0 (original)."
}
