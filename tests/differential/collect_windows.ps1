param(
    [int]$Repeat = 3,
    [switch]$UpdateGolden
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($Repeat -lt 2 -or $Repeat -gt 20) { throw 'Repeat must be between 2 and 20.' }

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Golden = Join-Path $Here 'golden\windows-v2.0.26-x64.jsonl'
$ManifestPath = Join-Path $Here 'golden\windows-v2.0.26-x64.manifest.json'
$Suite = Join-Path $Here 'trace_suite.ahk'
$ArchiveUrl = 'https://github.com/AutoHotkey/AutoHotkey/releases/download/v2.0.26/AutoHotkey_2.0.26.zip'
$ArchiveHash = '43522AA3122A57784AC5DB30ABF85C2244475C36ACD7796E2C993355F9E926AE'
$ExeHash = 'A2A54B8ABC476D7671D4DE0771BB54BF5F2373D79FF6871D0BA6A62C3B88AE00'
$TempRoot = Join-Path ([IO.Path]::GetTempPath()) ('ahk-differential-' + [guid]::NewGuid().ToString('N'))
$Zip = Join-Path $TempRoot 'AutoHotkey_2.0.26.zip'
$Portable = Join-Path $TempRoot 'portable'

try {
    New-Item -ItemType Directory -Path $TempRoot | Out-Null
    Invoke-WebRequest -Uri $ArchiveUrl -OutFile $Zip
    $actualArchive = (Get-FileHash $Zip -Algorithm SHA256).Hash
    if ($actualArchive -ne $ArchiveHash) {
        throw "Official archive hash mismatch: expected=$ArchiveHash actual=$actualArchive"
    }
    Expand-Archive -Path $Zip -DestinationPath $Portable
    $Exe = Join-Path $Portable 'AutoHotkey64.exe'
    $actualExe = (Get-FileHash $Exe -Algorithm SHA256).Hash
    if ($actualExe -ne $ExeHash) {
        throw "Portable executable hash mismatch: expected=$ExeHash actual=$actualExe"
    }

    $traces = @()
    $hashes = @()
    foreach ($index in 1..$Repeat) {
        $trace = Join-Path $TempRoot "windows-trace-$index.jsonl"
        $stdout = Join-Path $TempRoot "windows-trace-$index.stdout"
        $stderr = Join-Path $TempRoot "windows-trace-$index.stderr"
        $arguments = @(
            '/ErrorStdOut=UTF-8',
            ('"' + $Suite + '"'),
            ('"' + $trace + '"')
        )
        $process = Start-Process -FilePath $Exe -ArgumentList $arguments -Wait -PassThru `
            -NoNewWindow -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        if ($process.ExitCode -ne 0) {
            $message = Get-Content $stderr -Raw -ErrorAction SilentlyContinue
            throw "Windows trace run $index failed with exit=$($process.ExitCode): $message"
        }
        if (-not (Test-Path $trace)) { throw "Windows trace run $index produced no trace." }
        $traces += $trace
        $hashes += (Get-FileHash $trace -Algorithm SHA256).Hash
    }
    if (@($hashes | Select-Object -Unique).Count -ne 1) {
        throw "Windows golden is nondeterministic across $Repeat runs: $($hashes -join ',')"
    }

    if ($UpdateGolden) {
        Copy-Item $traces[0] $Golden -Force
        $manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
        $manifest.trace_sha256 = $hashes[0]
        $manifest.repeat_runs = $Repeat
        $manifest.byte_identical = $true
        $manifest.collected_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
        $manifest | ConvertTo-Json -Depth 8 | Set-Content $ManifestPath -Encoding utf8
    }
    else {
        $committedHash = (Get-FileHash $Golden -Algorithm SHA256).Hash
        if ($hashes[0] -ne $committedHash) {
            throw "Committed golden differs: committed=$committedHash observed=$($hashes[0])"
        }
        $manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
        if ($manifest.archive_sha256 -ne $ArchiveHash -or
            $manifest.executable_sha256 -ne $ExeHash -or
            $manifest.trace_sha256 -ne $committedHash) {
            throw 'Committed Windows manifest does not match verified archive/executable/trace hashes.'
        }
    }
    Write-Output "WINDOWS_GOLDEN_COLLECT_PASS runtime=2.0.26 repeats=$Repeat trace_sha256=$($hashes[0])"
}
finally {
    if (Test-Path $TempRoot) { Remove-Item -LiteralPath $TempRoot -Recurse -Force }
}
