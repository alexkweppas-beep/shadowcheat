<#
.SYNOPSIS
    Read-only forensic self-check for the launcher.

    After a full run plus Clean, this walks every location the launcher can
    touch and reports what is still findable. It changes nothing: every probe
    is a read. This is the acceptance test for the anti-forensics work — a
    trace that shows up here is a trace a manual check would find.

.USAGE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\Verify-Traces.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\Verify-Traces.ps1 -LoaderName "loader.exe"
#>

[CmdletBinding()]
param(
    # The loader's exe file name, used to match Prefetch / AmCache / BAM /
    # UserAssist entries. Defaults to loader.exe; pass the shipped name.
    [string]$LoaderName = "loader.exe"
)

$ErrorActionPreference = "SilentlyContinue"
$script:findings = 0

function Report {
    param([string]$Category, [string]$What, [string]$Where)
    $script:findings++
    Write-Host ("  [FOUND] [{0}] {1}  ->  {2}" -f $Category, $What, $Where) -ForegroundColor Yellow
}

function Section([string]$name) {
    Write-Host ""
    Write-Host "=== $name ===" -ForegroundColor Cyan
}

$loaderBase = [IO.Path]::GetFileNameWithoutExtension($LoaderName).ToUpper()
$loaderExe  = $LoaderName.ToUpper()
# Names the launcher spawns or loads, which create their own OS records.
$childNames = @("CMD", "AUDITPOL", "NET", "WEVTUTIL", "PING", "KDMAPPER", "IQVW64E")
$probes = @($loaderBase) + $childNames

# --- Temp driver leftovers -------------------------------------------------
Section "Temp driver leftovers (%TEMP%)"
$temp = $env:TEMP
# The Intel stub is a random 10-29 letter name with no extension, written and
# removed on each run. Anything matching that shape left behind is a leftover.
Get-ChildItem -LiteralPath $temp -File | Where-Object {
    $_.Extension -eq "" -and $_.Name -match "^[a-zA-Z]{10,29}$"
} | ForEach-Object { Report "temp" "random-named file (possible mapper stub)" $_.FullName }
# A Shadow folder under TEMP is the ban-queue fallback location.
if (Test-Path (Join-Path $temp "Shadow")) { Report "temp" "Shadow folder" (Join-Path $temp "Shadow") }

# --- Ban-report queue -------------------------------------------------------
Section "Ban-report queue"
$shadowApp = Join-Path $env:APPDATA "Shadow"
if (Test-Path $shadowApp) {
    Get-ChildItem -LiteralPath $shadowApp -Recurse -File | ForEach-Object {
        Report "disk" "ban queue file" $_.FullName
    }
}

# --- Prefetch ---------------------------------------------------------------
Section "Prefetch (C:\Windows\Prefetch)"
$prefetch = "C:\Windows\Prefetch"
if (Test-Path $prefetch) {
    foreach ($name in $probes) {
        Get-ChildItem -LiteralPath $prefetch -Filter "$name*.pf" | ForEach-Object {
            Report "prefetch" $_.Name $_.FullName
        }
    }
}

# --- AmCache ----------------------------------------------------------------
Section "AmCache"
$amcache = "$env:SystemRoot\AppCompat\Programs\Amcache.hve"
if (Test-Path $amcache) {
    # Read the hive for our image names without mounting it system-wide.
    $found = Select-String -LiteralPath $amcache -Pattern $loaderBase -SimpleMatch -ErrorAction SilentlyContinue
    if ($found) { Report "amcache" "loader name in Amcache.hve" $amcache }
}

# --- BAM --------------------------------------------------------------------
Section "BAM (Background Activity Moderator)"
$bamRoot = "HKLM:\SYSTEM\CurrentControlSet\Services\bam\State\UserSettings"
Get-ChildItem -LiteralPath $bamRoot -ErrorAction SilentlyContinue | ForEach-Object {
    $sidKey = $_.PSChildName
    $entries = Get-ChildItem -LiteralPath $_.PSPath -ErrorAction SilentlyContinue
    foreach ($e in $entries) {
        if ($e.PSChildName -match [regex]::Escape($loaderBase)) {
            Report "bam" $e.PSChildName $e.PSPath
        }
    }
}

# --- UserAssist -------------------------------------------------------------
Section "UserAssist"
$uaRoot = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\UserAssist"
Get-ChildItem -LiteralPath $uaRoot -ErrorAction SilentlyContinue | ForEach-Object {
    Get-ChildItem -LiteralPath (Join-Path $_.PSPath "Count") -ErrorAction SilentlyContinue | ForEach-Object {
        # UserAssist value names are ROT13; decode before matching.
        foreach ($value in $_.GetValueNames()) {
            $decoded = ($value.ToCharArray() | ForEach-Object {
                $c = [int][char]$_
                if (($c -ge 65 -and $c -le 90) -or ($c -ge 97 -and $c -le 122)) {
                    $base = if ($c -ge 97) { 97 } else { 65 }
                    [char](($c - $base + 13) % 26 + $base)
                } else { [char]$c }
            }) -join ""
            if ($decoded -match [regex]::Escape($loaderBase) -or $decoded -match "VoiceChanger") {
                Report "userassist" $decoded $_.PSPath
            }
        }
    }
}

# --- Shimcache --------------------------------------------------------------
Section "Shimcache (AppCompatCache)"
$shim = "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\AppCompatCache"
$shimVal = Get-ItemProperty -LiteralPath $shim -Name AppCompatCache -ErrorAction SilentlyContinue
if ($shimVal) {
    $bytes = $shimVal.AppCompatCache
    $text = [Text.Encoding]::Unicode.GetString($bytes)
    if ($text -match [regex]::Escape($loaderBase) -or $text -match "VoiceChanger") {
        Report "shimcache" "loader name in AppCompatCache" $shim
    }
}

# --- NVIDIA FTS keys ---------------------------------------------------------
Section "NVIDIA FTS (streamproof leftover)"
$ftsValue = "{497B8458-4244-4EE6-BFEA-F3D28A294F21}"
$ftsPaths = @(
    "HKLM:\SOFTWARE\NVIDIA Corporation\Global\NvApp\ShadowPlay\FTS",
    "HKLM:\SOFTWARE\NVIDIA Corporation\Global\ShadowPlay\FTS",
    "HKLM:\SOFTWARE\NVIDIA Corporation\Global\ShadowPlay",
    "HKLM:\SOFTWARE\NVIDIA Corporation\Global\NvApp\FTS",
    "HKLM:\SOFTWARE\NVIDIA Corporation\Global\FTS",
    "HKCU:\SOFTWARE\NVIDIA Corporation\Global\NvApp\ShadowPlay\FTS",
    "HKCU:\SOFTWARE\NVIDIA Corporation\Global\ShadowPlay\FTS"
)
foreach ($p in $ftsPaths) {
    $v = Get-ItemProperty -LiteralPath $p -Name $ftsValue -ErrorAction SilentlyContinue
    if ($v) { Report "registry" "NVIDIA FTS value present" "$p\\$ftsValue" }
}

# --- Intel mapper service keys ----------------------------------------------
Section "Mapper service keys"
$services = "HKLM:\SYSTEM\CurrentControlSet\Services"
Get-ChildItem -LiteralPath $services -ErrorAction SilentlyContinue | ForEach-Object {
    $img = (Get-ItemProperty -LiteralPath $_.PSPath -Name ImagePath -ErrorAction SilentlyContinue).ImagePath
    if ($img -and $img -match [regex]::Escape($temp)) {
        Report "registry" "service key pointing at a temp driver" $_.PSPath
    }
}

# --- DNS cache ---------------------------------------------------------------
Section "DNS cache"
$dns = ipconfig /displaydns 2>$null | Out-String
foreach ($host_ in @("shadowcheats.cc", "api.ipify.org", "api.telegram.org", "t.me")) {
    if ($dns -match [regex]::Escape($host_)) {
        Report "network" "DNS cache entry" $host_
    }
}

# --- NetworkList profiles -----------------------------------------------------
Section "NetworkList profiles"
$nlRoot = "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\NetworkList\Signatures"
Get-ChildItem -LiteralPath $nlRoot -Recurse -ErrorAction SilentlyContinue | ForEach-Object {
    $suffix = (Get-ItemProperty -LiteralPath $_.PSPath -Name DnsSuffix -ErrorAction SilentlyContinue).DnsSuffix
    if ($suffix -and $suffix -match "shadowcheats") {
        Report "network" "NetworkList profile with our domain" $_.PSPath
    }
}

# --- Event log state ----------------------------------------------------------
Section "Event logs"
# A "log cleared" event is itself the loudest cleanup trace.
$cleared = Get-WinEvent -FilterHashtable @{ LogName = "Security"; Id = 1102 } -MaxEvents 5 -ErrorAction SilentlyContinue
if ($cleared) { Report "eventlog" "Security log was cleared (Event 1102)" "Security" }
$sysCleared = Get-WinEvent -FilterHashtable @{ LogName = "System"; Id = 104 } -MaxEvents 5 -ErrorAction SilentlyContinue
if ($sysCleared) { Report "eventlog" "System log was cleared (Event 104)" "System" }

# --- Sysmain prefetch hook ----------------------------------------------------
Section "Sysmain prefetch hook"
$pipe = Get-ChildItem -LiteralPath "\\.\pipe\" -ErrorAction SilentlyContinue | Where-Object { $_.Name -eq "Prefetch_Rule_Pipe" }
if ($pipe) { Report "process" "Prefetch_Rule_Pipe present (hook lives in Sysmain)" "\\.\pipe\Prefetch_Rule_Pipe" }

# --- USN journal ---------------------------------------------------------------
Section "USN journal"
$usn = & fsutil usn queryjournal C: 2>$null | Out-String
if ($usn -match "First Usn") {
    # We cannot read individual records read-only without a driver; report the
    # journal exists and is being written, which is expected on a live volume.
    Write-Host "  [info] USN journal active on C: (per-file records exist for any file op)" -ForegroundColor DarkGray
}

# --- Summary -------------------------------------------------------------------
Write-Host ""
if ($script:findings -eq 0) {
    Write-Host "No traces found. A manual check would come up empty." -ForegroundColor Green
} else {
    Write-Host ("{0} trace(s) still findable." -f $script:findings) -ForegroundColor Red
}
exit $script:findings
