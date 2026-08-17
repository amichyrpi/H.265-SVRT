$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$kiCadBin = 'C:\Program Files\KiCad\10.0\bin'
$kiCadCli = Join-Path $kiCadBin 'kicad-cli.exe'
$kiCadPython = Join-Path $kiCadBin 'python.exe'
$systemPython = (Get-Command python -ErrorAction Stop).Source

if (-not (Test-Path -LiteralPath $kiCadCli)) {
    throw "KiCad CLI not found at $kiCadCli"
}

Push-Location $projectRoot
try {
    & $kiCadPython 'tools/configure_production_project.py'
    & $kiCadPython 'tools/add_production_ground_planes.py'
    & $kiCadPython 'tools/normalize_production_legend.py'
    & $kiCadPython 'tools/restore_board2_microsd.py'
    & $kiCadPython 'tools/apply_production_connectivity_fixes.py'
    & $kiCadPython 'tools/reconcile_board2_schematic_parity.py'
    & $kiCadPython 'tools/extract_board2_electrical.py'
    & $systemPython 'tools/recover_board2_schematic.py'

    & $kiCadCli sch erc 'pcb-hmd-carrier-board2.kicad_sch' `
        -o 'output/reports/ERC-pcb-hmd-carrier-board2-recovered.rpt'
    $ercText = Get-Content -LiteralPath `
        'output/reports/ERC-pcb-hmd-carrier-board2-recovered.rpt' -Raw
    $unexpectedErc = [regex]::Matches($ercText, '(?m)^\[([^\]]+)\]') |
        Where-Object { $_.Groups[1].Value -ne 'isolated_pin_label' }
    if ($unexpectedErc.Count -ne 0) {
        throw "Board2 ERC contains unexpected violation categories"
    }

    & $kiCadCli sch export netlist 'pcb-hmd-carrier-board2.kicad_sch' `
        --format kicadxml -o 'output/pcb-hmd-carrier-board2-current.net.xml'
    if ($LASTEXITCODE -ne 0) { throw "Schematic netlist export failed with code $LASTEXITCODE" }

    # KiCad returns a violation exit code while unrouted items remain.  Preserve
    # the report, then enforce the expected geometry result explicitly.
    & $kiCadCli pcb drc 'pcb-hmd-carrier-board2.kicad_pcb' `
        -o 'output/reports/DRC-pcb-hmd-carrier-board2-production-current.rpt' `
        --schematic-parity --severity-all --exit-code-violations
    $drcExit = $LASTEXITCODE
    $drcText = Get-Content -LiteralPath `
        'output/reports/DRC-pcb-hmd-carrier-board2-production-current.rpt' -Raw
    if ($drcText -notmatch '\*\* Found 0 DRC violations \*\*') {
        throw "Board geometry DRC contains violations (KiCad exit $drcExit)"
    }
    $parityCategories = @(
        'net_conflict',
        'footprint_symbol_mismatch',
        'footprint_symbol_field_mismatch',
        'lib_footprint_mismatch'
    )
    foreach ($category in $parityCategories) {
        if ($drcText -match "\[$category\]") {
            throw "Board2 schematic parity/library check failed: $category"
        }
    }

    & $kiCadPython 'tools/export_board2_production_audit.py'
    if ($LASTEXITCODE -ne 0) { throw "Production audit export failed with code $LASTEXITCODE" }

    Write-Host 'Production checkpoint validated: geometry clean; unrouted release gate remains.'
}
finally {
    Pop-Location
}
