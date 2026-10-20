#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$SourceDir,
    [Parameter(Mandatory)][string]$OutputDir,
    [Parameter(Mandatory)][ValidateSet('tss-suite-windows-x64','tss-viewer-windows-x64')][string]$Product,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$ViewerVersion,
    [string]$RuntimeVersion = '0.0',
    [string]$RuntimePath,
    [string]$PrivateKeyPath,
    [string]$PublicKeyPath = (Join-Path $PSScriptRoot '../keys/release-public.txt'),
    [string]$Notes = 'Application update',
    [long]$Sequence = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(),
    [int]$ValidDays = 180,
    [switch]$AllowUnsignedBuild
)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $SourceDir).Path
$output = [IO.Path]::GetFullPath($OutputDir)
if ($output.StartsWith($source + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or $output -eq $source) { throw 'Update output must be outside the application payload.' }
foreach ($value in @($Version, $ViewerVersion, $RuntimeVersion)) { if ($value -notmatch '^\d{1,5}(\.\d{1,5}){1,3}$' -or ([version]$value).Major -gt 65535) { throw "Invalid version: $value" } }
if ($Sequence -le 0 -or $ValidDays -lt 1 -or $ValidDays -gt 365) { throw 'Invalid release sequence or expiration' }
foreach ($required in @('TSSUpdater.exe', 'TSSLogViewer.exe', 'VERSION.txt')) { if (!(Test-Path -LiteralPath (Join-Path $source $required) -PathType Leaf)) { throw "Missing $required" } }
if ($Product -eq 'tss-suite-windows-x64' -and !(Test-Path -LiteralPath (Join-Path $source 'TSSLimiterX.exe'))) { throw 'Missing configurator' }
$keyPem = if ($PrivateKeyPath) { [IO.File]::ReadAllText((Resolve-Path -LiteralPath $PrivateKeyPath).Path) } else { $env:TSS_UPDATE_PRIVATE_KEY }
[IO.File]::WriteAllBytes((Join-Path $source 'TSSUpdate.lock'), [byte[]]::new(0))
if (!$keyPem) {
    if (!$AllowUnsignedBuild) { throw 'TSS_UPDATE_PRIVATE_KEY is required to publish an update.' }
    # Обычные CI/PR сборки остаются рабочими, но не притворяются подписанным выпуском.
    $manifest = Join-Path $source 'TSSUpdate.manifest'
    if (Test-Path -LiteralPath $manifest) { Remove-Item -LiteralPath $manifest }
    Write-Output 'Unsigned development build: automatic updates are disabled.'
    return
}
New-Item -ItemType Directory -Path (Join-Path $output 'blobs') -Force | Out-Null
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('TSSUPDATER/1'); $lines.Add("product=$Product"); $lines.Add("sequence=$Sequence")
$lines.Add("expires=$([DateTimeOffset]::UtcNow.AddDays($ValidDays).ToUnixTimeSeconds())")
$lines.Add("version=$Version"); $lines.Add("viewerVersion=$ViewerVersion"); $lines.Add("runtime=$RuntimeVersion")
$lines.Add("notes=$([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Notes)))")
$files = [Collections.Generic.List[object]]::new()
foreach ($file in Get-ChildItem -LiteralPath $source -File -Recurse) {
    $relative = [IO.Path]::GetRelativePath($source, $file.FullName).Replace('\', '/')
    if ($relative -in @('TSSUpdate.lock','TSSUpdate.manifest') -or $relative.StartsWith('.tss-update/') -or $relative -match '^unins') { continue }
    $files.Add([pscustomobject]@{ File=$file; Path=$relative })
}
if ($RuntimePath) { $files.Add([pscustomobject]@{ File=(Get-Item -LiteralPath $RuntimePath); Path='Prerequisites/vc_redist.x64.exe' }) }
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$total = 0L
foreach ($item in $files | Sort-Object Path) {
    if (!$seen.Add($item.Path) -or $item.Path.Length -gt 180 -or $item.Path -notmatch '^[A-Za-z0-9_./-]+$') { throw "Unsafe/duplicate package path: $($item.Path)" }
    foreach ($segment in $item.Path.Split('/')) { if (!$segment -or $segment.StartsWith('.') -or $segment.EndsWith('.') -or $segment -match '^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)') { throw "Unsafe path: $($item.Path)" } }
    if ($item.File.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse points are not supported' }
    $total += $item.File.Length
    if ($item.File.Length -gt 1GB -or $total -gt 4GB -or $seen.Count -gt 20000) { throw 'Update package exceeds client limits' }
    $hash = (Get-FileHash -LiteralPath $item.File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $blob = Join-Path $output "blobs/$hash.bin"
    if (!(Test-Path -LiteralPath $blob)) { Copy-Item -LiteralPath $item.File.FullName -Destination $blob }
    if ((Get-FileHash -LiteralPath $blob -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) { throw 'Blob hash mismatch' }
    $lines.Add("file`t$($item.File.Length)`t$hash`t$($item.Path)")
}
$payload = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n") + "`n")
$rsa = [Security.Cryptography.RSA]::Create()
try {
    $rsa.ImportFromPem($keyPem)
    if ($rsa.KeySize -lt 3072) { throw 'Signing key must be at least 3072 bits' }
    $parameters = $rsa.ExportParameters($false)
    $stream = [IO.MemoryStream]::new(); $writer = [IO.BinaryWriter]::new($stream)
    foreach ($value in @(0x31415352, $rsa.KeySize, $parameters.Exponent.Length, $parameters.Modulus.Length, 0, 0)) { $writer.Write([uint32]$value) }
    $writer.Write($parameters.Exponent); $writer.Write($parameters.Modulus)
    $actualKey = [Convert]::ToBase64String($stream.ToArray())
    $writer.Dispose(); $stream.Dispose()
    if ($actualKey -cne [IO.File]::ReadAllText((Resolve-Path -LiteralPath $PublicKeyPath).Path).Trim()) { throw 'Signing key does not match the public key pinned in the application.' }
    $signature = $rsa.SignData($payload, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pss)
} finally { $rsa.Dispose(); $keyPem = $null }
$envelope = [Convert]::ToBase64String($signature) + "`n" + [Text.Encoding]::UTF8.GetString($payload)
if ([Text.Encoding]::UTF8.GetByteCount($envelope) -gt 4MB) { throw 'Manifest exceeds client limit' }
[IO.File]::WriteAllText((Join-Path $output 'latest.tssupdate'), $envelope, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $source 'TSSUpdate.manifest'), $envelope, [Text.UTF8Encoding]::new($false))
Write-Output "Signed $Product release $Sequence; $($files.Count) files. Publish blobs first and latest.tssupdate last."
