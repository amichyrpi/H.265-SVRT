$ErrorActionPreference = "Stop"

# Official KiCad package models used by this project. Keeping local copies makes
# the project render identically on machines where the optional 3D library package
# is not installed.
$models = @(
    "Capacitor_SMD.3dshapes/C_0402_1005Metric.step",
    "Capacitor_SMD.3dshapes/C_0603_1608Metric.step",
    "Connector_JST.3dshapes/JST_GH_SM04B-GHS-TB_1x04-1MP_P1.25mm_Horizontal.step",
    "Connector_JST.3dshapes/JST_SH_SM06B-SRSS-TB_1x06-1MP_P1.00mm_Horizontal.step",
    "Connector_JST.3dshapes/JST_VH_B6PS-VH_1x06_P3.96mm_Horizontal.step",
    "Connector_PinHeader_1.27mm.3dshapes/PinHeader_2x05_P1.27mm_Vertical_SMD.step",
    "Crystal.3dshapes/Crystal_SMD_3225-4Pin_3.2x2.5mm.step",
    "Inductor_SMD.3dshapes/L_0402_1005Metric.step",
    "Inductor_SMD.3dshapes/L_0603_1608Metric.step",
    "Package_DFN_QFN.3dshapes/QFN-48-1EP_7x7mm_P0.5mm_EP5.7x5.7mm.step",
    "Package_DFN_QFN.3dshapes/Texas_RHB0032M_VQFN-32-1EP_5x5mm_P0.5mm_EP2.1x2.1mm.step",
    "Package_DFN_QFN.3dshapes/Texas_RJE0020A_VQFN-20-1EP_3x3mm_P0.45mm_EP0.675x0.76mm.step",
    "Package_DFN_QFN.3dshapes/Texas_RQM0029A_VQFN-29_4x4mm_P0.4mm.step",
    "Package_DFN_QFN.3dshapes/Texas_RVC0020A_WQFN-20-1EP_3x4mm_P0.5mm_EP1.6x2.6mm.step",
    "Package_DFN_QFN.3dshapes/Texas_X2QFN-12_1.6x1.6mm_P0.4mm.step",
    "Package_SO.3dshapes/TSSOP-8_3x3mm_P0.65mm.step",
    "Package_SON.3dshapes/USON-10_2.5x1.0mm_P0.5mm.step",
    "Package_SON.3dshapes/WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm.step",
    "Package_TO_SOT_SMD.3dshapes/SOT-23-5.step",
    "Package_TO_SOT_SMD.3dshapes/SOT-23.step",
    "Resistor_SMD.3dshapes/R_0402_1005Metric.step",
    "Sensor_Audio.3dshapes/Infineon_PG-LLGA-5-2.step"
)

$projectRoot = Split-Path -Parent $PSScriptRoot
$modelRoot = Join-Path $projectRoot "libraries/3dmodels/KiCad"
$baseUrl = "https://gitlab.com/kicad/libraries/kicad-packages3D/-/raw/master"
$verified = 0
$missing = @()

foreach ($relative in $models) {
    $destination = Join-Path $modelRoot $relative
    $directory = Split-Path -Parent $destination
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    if (-not (Test-Path -LiteralPath $destination)) {
        try {
            Invoke-WebRequest -Uri "$baseUrl/$relative" -OutFile $destination
        }
        catch {
            $missing += $relative
            if (Test-Path -LiteralPath $destination) {
                Remove-Item -LiteralPath $destination -Force
            }
            continue
        }
    }
    $header = Get-Content -LiteralPath $destination -TotalCount 1
    if ($header -ne "ISO-10303-21;") {
        throw "Downloaded file is not a STEP model: $relative"
    }
    $verified++
}

Write-Output "Verified $verified official KiCad STEP models in $modelRoot"
if ($missing.Count) {
    Write-Output "No official KiCad STEP was published for these exact footprint names:"
    $missing | ForEach-Object { Write-Output "  $_" }
}
