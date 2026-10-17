$ErrorActionPreference = 'Stop'
$sdkDir = Join-Path $PSScriptRoot 'luban-lite-main'
$configPath = Join-Path $sdkDir 'target\d12x\demo68-nor\pack\image_cfg.json'
$match = [regex]::Match((Get-Content -LiteralPath $configPath -Raw), '"version"\s*:\s*"(\d+\.\d+\.\d+)"')
if (-not $match.Success) { throw 'Cannot read the image version from image_cfg.json' }
$version = $match.Groups[1].Value
$filename = "d12x_demo68-nor_v$version.img"
$sourcePath = Join-Path $sdkDir "output\d12x_demo68-nor_rt-thread_helloworld\images\$filename"
$archiveDir = Join-Path $PSScriptRoot 'ing'
$targetPath = Join-Path $archiveDir $filename
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "Image missing: $sourcePath" }
if (-not (Test-Path -LiteralPath $archiveDir)) { New-Item -ItemType Directory -Path $archiveDir | Out-Null }
$sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
if (Test-Path -LiteralPath $targetPath) {
    if ((Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash -ne $sourceHash) {
        throw "Version $version already exists with different contents. Increment image_cfg.json version before publishing."
    }
} else {
    Copy-Item -LiteralPath $sourcePath -Destination $targetPath
}
if ((Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash -ne $sourceHash) { throw 'Image checksum mismatch' }
Write-Host "[OK] Firmware v$version : $targetPath"
Write-Host "[OK] SHA256: $sourceHash"
