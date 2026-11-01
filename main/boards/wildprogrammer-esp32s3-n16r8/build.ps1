[CmdletBinding()]
param(
    [ValidateSet("zh-CN", "en-US")]
    [string]$Language = "zh-CN",

    [string]$WakeWord = "custom:ni hao ling xi=你好灵溪;xiao ling xi=小灵溪;ling xi ling xi=灵溪灵溪;ling xi ni hao=灵溪你好;ling xi zai ma=灵溪在吗;ting zhi=停止;bi zui=闭嘴;tui chu=退出",

    [ValidateRange(1, 99)]
    [int]$WakeWordThreshold = 25,

    [string]$IdfProfile = $env:IDF_POWERSHELL_PROFILE,

    [string]$ComponentCache = (Join-Path ([Environment]::GetFolderPath("LocalApplicationData")) "Espressif\component-cache"),

    [string]$Config = "config.json"
)

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path

if ([string]::IsNullOrWhiteSpace($IdfProfile)) {
    throw "Pass -IdfProfile or set IDF_POWERSHELL_PROFILE."
}
if (-not (Test-Path -LiteralPath $IdfProfile -PathType Leaf)) {
    throw "ESP-IDF activation profile was not found: $IdfProfile"
}

. $IdfProfile
$env:PYTHONUTF8 = "1"
New-Item -ItemType Directory -Path $ComponentCache -Force | Out-Null
$env:IDF_COMPONENT_CACHE_PATH = $ComponentCache

Push-Location $repositoryRoot
try {
    & python scripts/build.py wildprogrammer-esp32s3-n16r8 `
        --config $Config `
        --language $Language `
        --wake-word $WakeWord `
        --wake-word-threshold $WakeWordThreshold
    if ($LASTEXITCODE -ne 0) {
        throw "Lingxi firmware build failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
