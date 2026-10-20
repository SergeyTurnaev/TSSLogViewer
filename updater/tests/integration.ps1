#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$Updater,
    [Parameter(Mandatory)][string]$CoreTests,
    [Parameter(Mandatory)][string]$PrivateKey,
    [Parameter(Mandatory)][string]$PublicKey,
    [Parameter(Mandatory)][string]$Workspace
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($Workspace)
if (Test-Path -LiteralPath $root) { throw 'Integration workspace must be new' }
New-Item -ItemType Directory -Path $root | Out-Null
$payload = Join-Path $root 'old'
$next = Join-Path $root 'new'
$install = Join-Path $root 'installation'
$feed = Join-Path $root 'feed'
$cache = Join-Path $root 'cache'
New-Item -ItemType Directory -Path $payload, $next, $install, $cache | Out-Null
$packager = Join-Path $PSScriptRoot '../tools/New-UpdatePackage.ps1'
function Write-Utf8([string]$Path, [string]$Value) { [IO.File]::WriteAllText($Path, $Value, [Text.UTF8Encoding]::new($false)) }
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw $Message } }
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Package([string]$Source, [string]$Output, [long]$Sequence, [string]$Version) {
    & $packager -SourceDir $Source -OutputDir $Output -Product tss-viewer-windows-x64 -Version $Version -ViewerVersion $Version -Sequence $Sequence -PrivateKeyPath $PrivateKey -PublicKeyPath $PublicKey
}
function Begin-Apply {
    param([string]$ExpectedHash = '')
    $token = [guid]::NewGuid().ToString('N')
    $process = Start-Process -FilePath (Join-Path $install 'TSSUpdater.exe') -ArgumentList "--apply `"$cache`" $token $ExpectedHash" -WindowStyle Hidden -PassThru
    Assert ($process.WaitForExit(10000)) 'Updater launcher did not exit'
    Assert ($process.ExitCode -eq 0) 'Updater launcher failed'
    return $token
}
function Await-Result([string]$Token) {
    $result = Join-Path $install ".tss-update/result-$Token.txt"
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (!(Test-Path -LiteralPath $result)) {
        if ([DateTime]::UtcNow -gt $deadline) { throw 'Updater did not finish in 30 seconds' }
        Start-Sleep -Milliseconds 100
    }
    $pidFile = Join-Path $install ".tss-update/pid-$Token.txt"
    if (Test-Path -LiteralPath $pidFile) { Wait-Process -Id ([int](Get-Content -LiteralPath $pidFile)) -Timeout 10 -ErrorAction SilentlyContinue }
    return [IO.File]::ReadAllText($result)
}
function Assert-Old {
    Assert ([IO.File]::ReadAllText((Join-Path $install 'TSSLogViewer.exe')) -eq 'old application') 'Application was not rolled back'
    Assert ([IO.File]::ReadAllText((Join-Path $install 'a.dll')) -eq 'old a') 'Changed DLL was not rolled back'
    Assert ((Hash (Join-Path $install 'TSSUpdate.manifest')) -eq (Hash (Join-Path $payload 'TSSUpdate.manifest'))) 'Installed manifest was not rolled back'
    Assert (!(Test-Path -LiteralPath (Join-Path $install '.tss-update/journal'))) 'Recovery journal remains after rollback'
    Assert ([IO.File]::ReadAllText((Join-Path $install 'user-data.txt')) -eq 'preserve') 'Unmanaged data was changed'
}
Copy-Item -LiteralPath $Updater -Destination (Join-Path $payload 'TSSUpdater.exe')
Write-Utf8 (Join-Path $payload 'TSSLogViewer.exe') 'old application'
Write-Utf8 (Join-Path $payload 'VERSION.txt') '26.1'
Write-Utf8 (Join-Path $payload 'a.dll') 'old a'
Write-Utf8 (Join-Path $payload 'z.dll') 'old z'
Write-Utf8 (Join-Path $payload 'obsolete.dll') 'obsolete'
Package $payload (Join-Path $root 'old-feed') 100 '26.1'
Copy-Item -Path "$payload/*" -Destination $install -Recurse
Write-Utf8 (Join-Path $install 'user-data.txt') 'preserve'
Copy-Item -Path "$payload/*" -Destination $next -Recurse
Remove-Item -LiteralPath (Join-Path $next 'obsolete.dll')
Write-Utf8 (Join-Path $next 'TSSLogViewer.exe') 'new application'
Write-Utf8 (Join-Path $next 'VERSION.txt') '26.2'
Write-Utf8 (Join-Path $next 'a.dll') 'new a'
Write-Utf8 (Join-Path $next 'z.dll') 'new z'
Write-Utf8 (Join-Path $next 'added.dll') 'new file'
# Допустимый PE overlay меняет хеш обновлятора и проверяет его собственную замену.
$helper = Join-Path $next 'TSSUpdater.exe'
$stream = [IO.File]::Open($helper, [IO.FileMode]::Append); $stream.WriteByte(0); $stream.Dispose()
Package $next $feed 200 '26.2'
Copy-Item -LiteralPath (Join-Path $feed 'latest.tssupdate') -Destination (Join-Path $cache 'release.manifest')
Copy-Item -LiteralPath (Join-Path $feed 'blobs') -Destination $cache -Recurse
& $CoreTests --verify (Join-Path $feed 'latest.tssupdate')
Assert ($LASTEXITCODE -eq 0) 'Valid RSA-PSS signature was rejected'

$validEnvelope = [IO.File]::ReadAllText((Join-Path $cache 'release.manifest'))
Write-Utf8 (Join-Path $cache 'release.manifest') ($validEnvelope.Replace('version=26.2', 'version=99.9'))
$result = Await-Result (Begin-Apply)
Assert ($result -like '*signature*') 'Tampered manifest was not rejected'
Assert-Old
Write-Utf8 (Join-Path $cache 'release.manifest') $validEnvelope

Assert ((Await-Result (Begin-Apply -ExpectedHash ('0' * 64))) -like '*selected release changed*') 'A different release was applied without confirmation'
Assert-Old

$blob = Join-Path $cache ('blobs/' + (Hash (Join-Path $next 'a.dll')) + '.bin')
Write-Utf8 $blob 'corrupt'
$result = Await-Result (Begin-Apply)
Assert ($result -like '*SHA-256*') 'Corrupt download was not rejected'
Assert-Old
Copy-Item -LiteralPath (Join-Path $next 'a.dll') -Destination $blob -Force

# Ошибка в середине применения: первый DLL уже заменён, второй занят другим процессом.
$locked = [IO.File]::Open((Join-Path $install 'z.dll'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try { $result = Await-Result (Begin-Apply) } finally { $locked.Dispose() }
Assert ($result -ne 'OK') 'Locked file should make update fail'
Assert-Old
Assert (!(Test-Path -LiteralPath (Join-Path $install 'added.dll'))) 'New file remained after rollback'

# Прерывание после записи журнала моделируется без завершения чужих процессов.
$backup = Join-Path $install '.tss-update/backup'
foreach ($name in @('a.dll','TSSLogViewer.exe')) { Copy-Item -LiteralPath (Join-Path $payload $name) -Destination (Join-Path $backup $name) -Force }
$journal = ''
foreach ($name in @('a.dll','TSSLogViewer.exe')) {
    $file = Get-Item -LiteralPath (Join-Path $backup $name)
    $journal += "R`t$($file.Length)`t$(Hash $file.FullName)`t$name`n"
}
Write-Utf8 (Join-Path $install '.tss-update/journal') $journal
Write-Utf8 (Join-Path $install 'a.dll') 'interrupted replacement'
Remove-Item -LiteralPath (Join-Path $install 'TSSLogViewer.exe')
$token = [guid]::NewGuid().ToString('N')
$process = Start-Process -FilePath (Join-Path $install 'TSSUpdater.exe') -ArgumentList "--recover $token" -WindowStyle Hidden -PassThru -Wait
Assert ($process.ExitCode -eq 0) 'Recovery launcher failed'
Assert ((Await-Result $token) -eq 'OK') 'Recovery failed'
Assert-Old

# Обе программы используют общую блокировку. Обновлятор ждёт, никого не завершает.
$appLock = [IO.File]::Open((Join-Path $install 'TSSUpdate.lock'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try {
    $token = Begin-Apply -ExpectedHash (Hash (Join-Path $cache 'release.manifest'))
    Start-Sleep -Seconds 1
    Assert (!(Test-Path -LiteralPath (Join-Path $install ".tss-update/result-$token.txt"))) 'Updater ignored application lock'
    Assert-Old
} finally { $appLock.Dispose() }
Assert ((Await-Result $token) -eq 'OK') 'Update failed after applications closed'
foreach ($name in @('TSSLogViewer.exe','TSSUpdater.exe','a.dll','z.dll','added.dll','VERSION.txt','TSSUpdate.manifest')) { Assert ((Hash (Join-Path $install $name)) -eq (Hash (Join-Path $next $name))) "Installed file differs: $name" }
Assert (!(Test-Path -LiteralPath (Join-Path $install 'obsolete.dll'))) 'Obsolete managed file remained'
Assert ([IO.File]::ReadAllText((Join-Path $install 'user-data.txt')) -eq 'preserve') 'User data was changed'
Copy-Item -LiteralPath (Join-Path $payload 'TSSUpdate.manifest') -Destination (Join-Path $cache 'release.manifest') -Force
Assert ((Await-Result (Begin-Apply)) -like '*downgrade*') 'Downgrade was not rejected'
Write-Output 'PASS: RSA-PSS, tampering, corrupt download, locked DLL rollback, crash recovery, application lock, updater self-update, removal of obsolete files, preservation of user data, downgrade protection.'
