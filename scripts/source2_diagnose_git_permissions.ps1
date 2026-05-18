[CmdletBinding()]
param(
  [string]$RepoRoot = ".",
  [string]$BackupDir = "artifacts\migrationv3-git-acl",
  [switch]$WriteAclReport,
  [switch]$NoRecommendations
)

$ErrorActionPreference = "Stop"

function Resolve-PathForRepo {
  param(
    [string]$Base,
    [string]$Path
  )

  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }
  return [System.IO.Path]::GetFullPath((Join-Path $Base $Path))
}

function Get-IdentityName {
  try {
    return [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
  } catch {
    return [Environment]::UserName
  }
}

function Get-DirectorySecurity {
  param(
    [string]$Path
  )

  try {
    return Get-Acl -LiteralPath $Path -ErrorAction Stop
  } catch {
    return $null
  }
}

function Get-SecurityOwner {
  param(
    [AllowNull()]$Acl
  )

  if ($null -eq $Acl) {
    return "unavailable"
  }
  try {
    if ($Acl.PSObject.Properties.Name -contains "Owner") {
      return $Acl.Owner
    }
    return $Acl.GetOwner([System.Security.Principal.NTAccount]).Value
  } catch {
    try {
      return $Acl.GetOwner([System.Security.Principal.SecurityIdentifier]).Value
    } catch {
      return "unavailable"
    }
  }
}

function Get-SecurityGroup {
  param(
    [AllowNull()]$Acl
  )

  if ($null -eq $Acl) {
    return "unavailable"
  }
  try {
    if ($Acl.PSObject.Properties.Name -contains "Group") {
      return $Acl.Group
    }
    return $Acl.GetGroup([System.Security.Principal.NTAccount]).Value
  } catch {
    try {
      return $Acl.GetGroup([System.Security.Principal.SecurityIdentifier]).Value
    } catch {
      return "unavailable"
    }
  }
}

function Get-SecurityAccessRules {
  param(
    [AllowNull()]$Acl
  )

  if ($null -eq $Acl) {
    return @()
  }
  if ($Acl.PSObject.Properties.Name -contains "Access") {
    return @($Acl.Access)
  }
  return @($Acl.GetAccessRules($true, $true, [System.Security.Principal.SecurityIdentifier]))
}

function Get-IcaclsOutput {
  param(
    [string]$Path
  )

  try {
    return @(& icacls $Path 2>&1)
  } catch {
    return @("icacls failed: $($_.Exception.Message)")
  }
}

function Get-ExplicitDenyRuleText {
  param(
    [object[]]$AccessRules,
    [string[]]$IcaclsOutput
  )

  $rules = [System.Collections.Generic.List[string]]::new()
  foreach ($rule in $AccessRules) {
    if ($rule.AccessControlType -eq [System.Security.AccessControl.AccessControlType]::Deny -and
        -not $rule.IsInherited) {
      [void]$rules.Add((Format-AccessRule -Rule $rule))
    }
  }
  if ($rules.Count -gt 0) {
    return $rules.ToArray()
  }

  foreach ($line in $IcaclsOutput) {
    $text = [string]$line
    if ($text -match '\(DENY\)' -and $text -notmatch '\(I\).*?\(DENY\)') {
      [void]$rules.Add($text.Trim())
    }
  }
  return $rules.ToArray()
}

function Test-DirectoryWrite {
  param(
    [string]$Directory
  )

  $probePath = Join-Path $Directory ("source2_git_write_probe_{0}_{1}.tmp" -f $PID, [guid]::NewGuid().ToString("N"))
  $stream = $null
  try {
    $stream = [System.IO.File]::Open(
      $probePath,
      [System.IO.FileMode]::CreateNew,
      [System.IO.FileAccess]::ReadWrite,
      [System.IO.FileShare]::None)
    $stream.WriteByte(65)
    return [pscustomobject]@{
      Succeeded = $true
      Path = $probePath
      Message = "Created and wrote a temporary probe file."
    }
  } catch {
    $message = $_.Exception.Message
    if ($_.Exception.InnerException -and ![string]::IsNullOrWhiteSpace($_.Exception.InnerException.Message)) {
      $message = $_.Exception.InnerException.Message
    }
    return [pscustomobject]@{
      Succeeded = $false
      Path = $probePath
      Message = $message
    }
  } finally {
    if ($stream) {
      $stream.Close()
    }
    if (Test-Path -LiteralPath $probePath) {
      Remove-Item -LiteralPath $probePath -Force -ErrorAction SilentlyContinue
    }
  }
}

function Format-AccessRule {
  param(
    [System.Security.AccessControl.FileSystemAccessRule]$Rule
  )

  return "{0} {1} {2} inherited={3} inheritance={4} propagation={5}" -f `
    $Rule.IdentityReference.ToString(),
    $Rule.AccessControlType,
    $Rule.FileSystemRights,
    $Rule.IsInherited,
    $Rule.InheritanceFlags,
    $Rule.PropagationFlags
}

function Write-Recommendations {
  param(
    [string]$GitDir,
    [string[]]$ExplicitDenyRules
  )

  Write-Host "Recommended recovery:"
  Write-Host "1. Open a local PowerShell prompt as the owner of .git or an administrator."
  Write-Host "2. Inspect the ACL before changing it:"
  Write-Host "   icacls `"$GitDir`""
  if ($ExplicitDenyRules.Count -gt 0) {
    $sidList = $ExplicitDenyRules |
      ForEach-Object {
        if ($_ -match '^(?<identity>\S+)') {
          $Matches.identity
        }
      } |
      Where-Object { ![string]::IsNullOrWhiteSpace($_) } |
      Sort-Object -Unique
    Write-Host "3. After verifying the deny ACEs are stale, remove only those Deny ACEs:"
    foreach ($sid in $sidList) {
      Write-Host "   icacls `"$GitDir`" /remove:d `"$sid`""
    }
  } else {
    Write-Host "3. No explicit Deny ACEs were detected on .git; repair owner/allow rules for the committing account."
  }
  Write-Host "4. Re-run this helper, then run the phase commits from migrationv3\\phase_commit_plan.md."
}

$repo = [System.IO.Path]::GetFullPath($RepoRoot)
$gitDir = Join-Path $repo ".git"

Write-Host "== Source2 Git permission diagnosis =="
Write-Host "Repo root: $repo"
Write-Host "Current identity: $(Get-IdentityName)"

if (!(Test-Path -LiteralPath $gitDir -PathType Container)) {
  Write-Host "Git metadata directory not found: $gitDir"
  exit 1
}

$lockPath = Join-Path $gitDir "index.lock"
if (Test-Path -LiteralPath $lockPath) {
  Write-Host "Git index lock: present at $lockPath"
} else {
  Write-Host "Git index lock: absent"
}

$acl = Get-DirectorySecurity -Path $gitDir
$owner = Get-SecurityOwner -Acl $acl
$group = Get-SecurityGroup -Acl $acl
$icaclsOutput = Get-IcaclsOutput -Path $gitDir
Write-Host "Git owner: $owner"
Write-Host "Git group: $group"

$explicitDenyRules = @(Get-ExplicitDenyRuleText -AccessRules (Get-SecurityAccessRules -Acl $acl) -IcaclsOutput $icaclsOutput)

if ($explicitDenyRules.Count -gt 0) {
  Write-Host "Explicit Deny ACEs on .git:"
  foreach ($rule in $explicitDenyRules) {
    Write-Host "  $rule"
  }
} else {
  Write-Host "Explicit Deny ACEs on .git: none"
}

$writeProbe = Test-DirectoryWrite -Directory $gitDir
if ($writeProbe.Succeeded) {
  Write-Host "Git metadata write probe: passed"
} else {
  Write-Host "Git metadata write probe: failed"
  Write-Host "Write error: $($writeProbe.Message)"
}

if ($WriteAclReport) {
  $backupRoot = Resolve-PathForRepo -Base $repo -Path $BackupDir
  New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
  $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
  $reportPath = Join-Path $backupRoot "$stamp-git-acl-report.txt"
  $report = @(
    "repo=$repo",
    "git_dir=$gitDir",
    "identity=$(Get-IdentityName)",
    "owner=$owner",
    "group=$group",
    "index_lock_present=$(Test-Path -LiteralPath $lockPath)",
    "write_probe_succeeded=$($writeProbe.Succeeded)",
    "write_probe_message=$($writeProbe.Message)",
    "",
    "explicit deny rules:",
    $(if ($explicitDenyRules.Count -gt 0) {
        $explicitDenyRules
      } else {
        "(none)"
      }),
    "",
    "icacls:",
    $icaclsOutput
  )
  Set-Content -LiteralPath $reportPath -Value $report -Encoding ascii
  Write-Host "ACL report: $reportPath"
}

if (!$writeProbe.Succeeded) {
  if (!$NoRecommendations) {
    Write-Recommendations -GitDir $gitDir -ExplicitDenyRules $explicitDenyRules
  }
  exit 1
}

Write-Host "Git metadata is writable for this process."
exit 0
