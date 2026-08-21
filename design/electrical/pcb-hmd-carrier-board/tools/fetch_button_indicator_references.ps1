$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$reference = Join-Path $root 'reference'
New-Item -ItemType Directory -Force -Path $reference | Out-Null

$headers = @{ 'User-Agent' = 'Mozilla/5.0 Stearlight-CAD/1.0' }
$hirose = 'https://www.hirose.com/en/product/document?clcode=CL0673-5040-0-51&documentid=0001141876&documenttype=2DDrawing&lang=en&productname=BM28B0.6-20DS%2F2-0.35V%2851%29&series=BM28'
$led = 'https://datasheet.lcsc.com/datasheet/pdf/6c5298b5365ee76581cb6473eef16a54.pdf?productCode=C601674'

Invoke-WebRequest -UseBasicParsing -Headers $headers -Uri $hirose -OutFile (Join-Path $reference 'Hirose_BM28B0.6-20DS_drawing.pdf')
Invoke-WebRequest -UseBasicParsing -Headers $headers -Uri $led -OutFile (Join-Path $reference 'TOGIALED_TJ-S3227SW1TCGLCCYRGB-A5.pdf')

Write-Host 'Fetched official Hirose and TOGIALED reference documents.'
