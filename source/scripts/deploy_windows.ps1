[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,

    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',

    [Parameter(Mandatory = $true)]
    [string]$QtBinDirectory,

    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist\AtlasLauncher-win64'),

    [string]$OpenSslRuntimeDirectory = (Join-Path $PSScriptRoot '..\third_party\openssl-win64')
)

$ErrorActionPreference = 'Stop'
$resolvedBuild = (Resolve-Path $BuildDirectory).Path
$resolvedQtBin = (Resolve-Path $QtBinDirectory).Path
$resolvedOpenSsl = (Resolve-Path $OpenSslRuntimeDirectory).Path
$exe = Join-Path $resolvedBuild "${Configuration}\AtlasLauncher.exe"
if (-not (Test-Path $exe)) {
    $exe = Join-Path $resolvedBuild 'AtlasLauncher.exe'
}
if (-not (Test-Path $exe)) {
    throw "Не найден AtlasLauncher.exe. Сначала соберите конфигурацию $Configuration."
}
$probe = Join-Path $resolvedBuild "${Configuration}\TlsRuntimeProbe.exe"
if (-not (Test-Path $probe)) {
    $probe = Join-Path $resolvedBuild 'TlsRuntimeProbe.exe'
}
if (-not (Test-Path $probe)) {
    throw "Не найден TlsRuntimeProbe.exe. Сначала соберите все Windows-цели Atlas."
}

$deployTool = Join-Path $resolvedQtBin 'windeployqt.exe'
if (-not (Test-Path $deployTool)) {
    throw "Не найден windeployqt.exe: $deployTool"
}

$requiredOpenSslFiles = @(
    'bin\libcrypto-1_1-x64.dll',
    'bin\libssl-1_1-x64.dll',
    'LICENSE.txt',
    'VERSION.txt',
    'SHA256SUMS.txt'
)
foreach ($relativePath in $requiredOpenSslFiles) {
    $source = Join-Path $resolvedOpenSsl $relativePath
    if (-not (Test-Path $source)) {
        throw "Не найден обязательный runtime-файл OpenSSL: $source. Сначала выполните scripts\build_openssl_win64.sh."
    }
}

if (Test-Path $OutputDirectory) {
    Remove-Item -Path $OutputDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Copy-Item -Path $exe -Destination $OutputDirectory -Force
Copy-Item -Path $probe -Destination (Join-Path $OutputDirectory 'AtlasTLSProbe.exe') -Force

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$toolsSource = Join-Path $repoRoot 'tools'
$toolsDestination = Join-Path $OutputDirectory 'tools'
$requiredToolFiles = @('7za.exe', '7za.dll', '7zip-LICENSE.txt', '7zip-SHA256.txt')
foreach ($toolFile in $requiredToolFiles) {
    $source = Join-Path $toolsSource $toolFile
    if (-not (Test-Path $source)) {
        throw "Не найден обязательный инструмент локальной Java Runtime: $source"
    }
}
New-Item -ItemType Directory -Path $toolsDestination -Force | Out-Null
Copy-Item -Path (Join-Path $toolsSource '*') -Destination $toolsDestination -Force

& $deployTool --release --compiler-runtime --no-translations (Join-Path $OutputDirectory 'AtlasLauncher.exe')
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt завершился с кодом $LASTEXITCODE"
}

# Qt 5.15 resolves OpenSSL dynamically at runtime. Keep both DLLs beside the
# executable, which is the Windows DLL search location used by QSslSocket.
Copy-Item -Path (Join-Path $resolvedOpenSsl 'bin\libcrypto-1_1-x64.dll') -Destination $OutputDirectory -Force
Copy-Item -Path (Join-Path $resolvedOpenSsl 'bin\libssl-1_1-x64.dll') -Destination $OutputDirectory -Force
$licenseDestination = Join-Path $OutputDirectory 'licenses\OpenSSL-1.1.1w'
New-Item -ItemType Directory -Path $licenseDestination -Force | Out-Null
Copy-Item -Path (Join-Path $resolvedOpenSsl 'LICENSE.txt') -Destination $licenseDestination -Force
Copy-Item -Path (Join-Path $resolvedOpenSsl 'VERSION.txt') -Destination $licenseDestination -Force
Copy-Item -Path (Join-Path $resolvedOpenSsl 'SHA256SUMS.txt') -Destination $licenseDestination -Force

$portableOpenSslFiles = @(
    (Join-Path $OutputDirectory 'libcrypto-1_1-x64.dll'),
    (Join-Path $OutputDirectory 'libssl-1_1-x64.dll')
)
foreach ($runtimeFile in $portableOpenSslFiles) {
    if (-not (Test-Path $runtimeFile)) {
        throw "TLS runtime не попал в portable-пакет: $runtimeFile"
    }
}

Write-Host "Готово: $OutputDirectory"
Write-Host "Добавлены Qt, MinGW runtime, tools\7za.exe и OpenSSL 1.1.1w DLL для HTTPS."
Write-Host "Перед распространением запустите AtlasTLSProbe.exe или откройте библиотеку на чистой Windows 7 SP1 x64: список версий Mojang должен загрузиться без TLS initialization failed."
