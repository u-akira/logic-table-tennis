param(
[string]$Port = "COM3"
)

$ErrorActionPreference = "Stop"

$Fqbn = "esp32:esp32:XIAO_ESP32C3"
$SketchPath = "."

Write-Host "=== Arduino Build ==="
Write-Host "FQBN : $Fqbn"
Write-Host "Port : $Port"

arduino-cli compile --fqbn $Fqbn $SketchPath

if ($LASTEXITCODE -ne 0) {
throw "Compile failed"
}

Write-Host ""
Write-Host "=== Arduino Upload ==="

arduino-cli upload -p $Port --fqbn $Fqbn $SketchPath

if ($LASTEXITCODE -ne 0) {
throw "Upload failed"
}

Write-Host ""
Write-Host "=== Done ==="
Write-Host "Upload completed successfully."
