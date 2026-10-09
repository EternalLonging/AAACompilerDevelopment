$ErrorActionPreference = 'Stop'
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding

# Run with the bundled pwsh.exe -NoLogo -NoProfile -File, not a nested -Command.
$workspaceRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$probePath = Join-Path $PSScriptRoot ('sandbox-probe-' + [guid]::NewGuid().ToString('N') + '.txt')
$probeText = 'UTF-8 roundtrip: ' + [char]0x4E2D + [char]0x6587
try {
    [System.IO.File]::WriteAllText($probePath, $probeText, [System.Text.UTF8Encoding]::new($false))
    $actualText = [System.IO.File]::ReadAllText($probePath, [System.Text.UTF8Encoding]::new($false))
    if ($actualText -cne $probeText) { throw 'UTF-8 roundtrip failed.' }
} finally {
    if (Test-Path -LiteralPath $probePath) { Remove-Item -LiteralPath $probePath -ErrorAction Stop }
}

$environmentReadable = $false
try {
    $null = Get-ChildItem Env: -ErrorAction Stop
    $environmentReadable = $true
} catch {
    # PowerShell 5.1 can fail if inherited variables differ only by case.
    $environmentReadable = $false
}

$setupErrorPath = Join-Path $env:USERPROFILE '.codex\.sandbox\setup_error.json'
$setupError = if (Test-Path -LiteralPath $setupErrorPath) {
    Get-Content -LiteralPath $setupErrorPath -Raw -Encoding UTF8 | ConvertFrom-Json
} else { $null }

$result = [ordered]@{
    powershell = $PSVersionTable.PSVersion.ToString()
    workspace = $workspaceRoot
    workspace_write_and_utf8 = 'PASS'
    probe_cleanup = -not (Test-Path -LiteralPath $probePath)
    environment_enumeration = $environmentReadable
    network_disabled_by_sandbox = [Environment]::GetEnvironmentVariable('CODEX_SANDBOX_NETWORK_DISABLED', 'Process')
    historical_setup_error_code = if ($setupError) { $setupError.code } else { $null }
    historical_setup_error_message = if ($setupError) { $setupError.message } else { $null }
    setup_note = 'Historical error markers alone do not establish the current sandbox state; inspect the latest setup log after a fresh initialization.'
}
$result | ConvertTo-Json -Depth 4
