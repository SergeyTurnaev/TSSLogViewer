#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$PrivateKeyPath,
    [Parameter(Mandatory)][string]$PublicKeyPath
)
$ErrorActionPreference = 'Stop'
if ((Test-Path -LiteralPath $PrivateKeyPath) -or (Test-Path -LiteralPath $PublicKeyPath)) { throw 'Key files already exist; refusing to replace trust keys.' }
$privatePath = [IO.Path]::GetFullPath($PrivateKeyPath)
$publicPath = [IO.Path]::GetFullPath($PublicKeyPath)
New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($privatePath)), ([IO.Path]::GetDirectoryName($publicPath)) | Out-Null
$rsa = [Security.Cryptography.RSA]::Create(3072)
try {
    [IO.File]::WriteAllText($privatePath, $rsa.ExportPkcs8PrivateKeyPem(), [Text.UTF8Encoding]::new($false))
    if ($IsWindows) {
        $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
        $acl = [Security.AccessControl.FileSecurity]::new()
        $acl.SetOwner($sid)
        $acl.SetAccessRuleProtection($true, $false)
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($sid, 'FullControl', 'Allow'))
        Set-Acl -LiteralPath $privatePath -AclObject $acl
    } else { & chmod 600 $privatePath; if ($LASTEXITCODE) { throw 'Cannot protect signing key' } }
    $parameters = $rsa.ExportParameters($false)
    $stream = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($stream)
    foreach ($value in @(0x31415352, 3072, $parameters.Exponent.Length, $parameters.Modulus.Length, 0, 0)) { $writer.Write([uint32]$value) }
    $writer.Write($parameters.Exponent); $writer.Write($parameters.Modulus)
    [IO.File]::WriteAllText($publicPath, [Convert]::ToBase64String($stream.ToArray()) + "`n", [Text.UTF8Encoding]::new($false))
    $writer.Dispose(); $stream.Dispose()
} finally { $rsa.Dispose() }
Write-Output 'Signing key created. Keep the private PEM offline and in the release secret TSS_UPDATE_PRIVATE_KEY; commit only the public key.'
