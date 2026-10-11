# Empaqueta DxilProbe como MSIX firmado (misma mecánica que dist\uwp\package.ps1 de Azahar-UWP).
#   .\package.ps1 -BuildDir build
# Compila los shaders de prueba (DXBC con fxc, DXIL con dxc), arma el layout y firma con un
# certificado autofirmado cuyo Subject coincide con el Publisher del manifiesto.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $BuildDir,
    [string] $Config = 'Release',
    [string] $Publisher = 'CN=DxilProbe'
)

$ErrorActionPreference = 'Stop'
$root   = $PSScriptRoot
$binDir = Join-Path $BuildDir "bin\$Config"
$out    = Join-Path $BuildDir 'AppPackages'
$layout = Join-Path $out 'layout'
$assets = Join-Path $layout 'Assets'
$pfx    = Join-Path $out 'dxilprobe.pfx'

function Get-SdkTool([string] $name) {
    $tool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\$name" -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | Select-Object -First 1
    if ($tool) { return $tool.FullName }
    return $null
}

# Ejecuta una herramienta nativa y devuelve su salida (sin que stderr aborte el script).
function Invoke-Tool([string] $exe, [string[]] $toolArgs) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $text = & $exe @toolArgs 2>&1 | Out-String
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) { throw "$exe falló ($code):`n$text" }
    return $text
}

New-Item -ItemType Directory -Force -Path $out | Out-Null
if (Test-Path $layout) { Remove-Item -Recurse -Force $layout }
New-Item -ItemType Directory -Force -Path $layout, $assets | Out-Null

# --- Herramientas de shaders ---------------------------------------------------------------
$fxc = Get-SdkTool 'fxc.exe'
$dxc = Get-SdkTool 'dxc.exe'
if (-not $dxc) {
    Write-Host '==> dxc.exe no está en el SDK; bajando Microsoft.Direct3D.DXC de NuGet'
    $zip = Join-Path $out 'dxc.zip'
    Invoke-WebRequest 'https://www.nuget.org/api/v2/package/Microsoft.Direct3D.DXC' -OutFile $zip
    $dxcDir = Join-Path $out 'dxc-pkg'
    Expand-Archive $zip -DestinationPath $dxcDir -Force
    $dxc = (Get-ChildItem $dxcDir -Recurse -Filter dxc.exe | Where-Object { $_.FullName -match 'x64' } |
            Select-Object -First 1).FullName
}
if (-not $fxc) { throw 'No se encontró fxc.exe en el Windows SDK.' }
if (-not $dxc) { throw 'No se encontró dxc.exe.' }
Write-Host "fxc: $fxc"
Write-Host "dxc: $dxc"

$cs  = Join-Path $root 'shaders\cs.hlsl'
$gfx = Join-Path $root 'shaders\gfx.hlsl'

Write-Host '==> Compilando DXBC (SM 5.1) con fxc'
Invoke-Tool $fxc @('/nologo', '/T', 'cs_5_1', '/E', 'main',   '/Fo', (Join-Path $assets 'cs_5_1.cso'), $cs)  | Out-Null
Invoke-Tool $fxc @('/nologo', '/T', 'vs_5_1', '/E', 'VSMain', '/Fo', (Join-Path $assets 'vs_5_1.cso'), $gfx) | Out-Null
Invoke-Tool $fxc @('/nologo', '/T', 'ps_5_1', '/E', 'PSMain', '/Fo', (Join-Path $assets 'ps_5_1.cso'), $gfx) | Out-Null

Write-Host '==> Compilando DXIL (SM 6.0) con dxc'
$dxilOut = ''
$dxilOut += Invoke-Tool $dxc @('-nologo', '-T', 'cs_6_0', '-E', 'main',   '-Fo', (Join-Path $assets 'cs_6_0.cso'), $cs)
$dxilOut += Invoke-Tool $dxc @('-nologo', '-T', 'vs_6_0', '-E', 'VSMain', '-Fo', (Join-Path $assets 'vs_6_0.cso'), $gfx)
$dxilOut += Invoke-Tool $dxc @('-nologo', '-T', 'ps_6_0', '-E', 'PSMain', '-Fo', (Join-Path $assets 'ps_6_0.cso'), $gfx)
# Sin dxil.dll junto a dxc.exe el DXIL sale SIN FIRMAR y el runtime lo rechazaría por esa razón,
# lo que daría un falso negativo en la prueba.
if ($dxilOut -match 'dxil\.dll|validation skipped|not signed') {
    throw "dxc no pudo firmar (validar) el DXIL; la prueba daría un falso negativo:`n$dxilOut"
}

# --- Layout ----------------------------------------------------------------------------------
$exe = Join-Path $binDir 'DxilProbe.exe'
if (-not (Test-Path $exe)) { throw "No existe $exe; compila primero." }
Copy-Item $exe $layout
Copy-Item (Join-Path $root 'dist\Package.appxmanifest') (Join-Path $layout 'AppxManifest.xml')

# Imágenes de mosaico (color sólido)
Add-Type -AssemblyName System.Drawing
$sizes = @{
    'StoreLogo.png' = @(50, 50);  'Square44x44Logo.png' = @(44, 44); 'Square150x150Logo.png' = @(150, 150)
    'Wide310x150Logo.png' = @(310, 150); 'SplashScreen.png' = @(620, 300)
}
foreach ($name in $sizes.Keys) {
    $w, $h = $sizes[$name]
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::FromArgb(255, 20, 90, 160))
    $g.Dispose()
    $bmp.Save((Join-Path $assets $name), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# --- Índice de recursos + MSIX -----------------------------------------------------------------
$makepri  = Get-SdkTool 'makepri.exe'
$makeappx = Get-SdkTool 'makeappx.exe'
$signtool = Get-SdkTool 'signtool.exe'
if (-not ($makepri -and $makeappx -and $signtool)) { throw 'Faltan makepri/makeappx/signtool del Windows SDK.' }

$priConfig = Join-Path $out 'priconfig.xml'
Invoke-Tool $makepri @('createconfig', '/cf', $priConfig, '/dq', 'en-US', '/o') | Out-Null
Invoke-Tool $makepri @('new', '/pr', $layout, '/cf', $priConfig, '/of', (Join-Path $layout 'resources.pri'), '/o') | Out-Null

$msix = Join-Path $out 'DxilProbe_1.0.0.0.msix'
if (Test-Path $msix) { Remove-Item -Force $msix }
Invoke-Tool $makeappx @('pack', '/d', $layout, '/p', $msix, '/o', '/nv') | Out-Null

# --- Firma -------------------------------------------------------------------------------------
if (-not (Test-Path $pfx)) {
    $cert = New-SelfSignedCertificate -Type Custom -Subject $Publisher -KeyUsage DigitalSignature `
        -FriendlyName 'DxilProbe (dev)' -CertStoreLocation 'Cert:\CurrentUser\My' `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
    $pwd = ConvertTo-SecureString -String 'dxilprobe' -Force -AsPlainText
    Export-PfxCertificate -Cert "Cert:\CurrentUser\My\$($cert.Thumbprint)" -FilePath $pfx -Password $pwd | Out-Null
    Export-Certificate -Cert "Cert:\CurrentUser\My\$($cert.Thumbprint)" -FilePath ([IO.Path]::ChangeExtension($pfx, '.cer')) | Out-Null
}
Invoke-Tool $signtool @('sign', '/fd', 'SHA256', '/a', '/f', $pfx, '/p', 'dxilprobe', $msix) | Out-Null

Write-Host "==> Paquete listo: $msix"
