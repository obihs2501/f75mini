param([Parameter(Mandatory)][string]$Exe)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
# Decode redirected native stdout as UTF-8, including Chinese device names.
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$file = Join-Path ([IO.Path]::GetTempPath()) ("F75 CI $([char]0x4e2d)$([char]0x6587) " + [guid]::NewGuid() + '.f75demo')
function ExpectExit([int]$Expected) {
    if ($LASTEXITCODE -ne $Expected) { throw "Expected $Expected, got $LASTEXITCODE" }
}
try {
    & $Exe demo create $file
    ExpectExit 0
    & $Exe demo validate $file
    ExpectExit 0
    & $Exe demo create $file
    ExpectExit 5
    & $Exe restore $file
    ExpectExit 2
    & $Exe info --device 0
    ExpectExit 2
    & $Exe demo validate '\\invalid\network\config.f75demo'
    ExpectExit 5
    $json = & $Exe list --json
    ExpectExit 0
    if (-not ($json -match '^\[.*\]$')) { throw 'Discovery output is not an array' }
    $null = ConvertFrom-Json -InputObject $json
} finally {
    if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
}
