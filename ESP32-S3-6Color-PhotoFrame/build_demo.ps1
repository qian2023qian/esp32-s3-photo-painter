. 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1' | Out-Null
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Set-Location 'E:\ESP32\ESP-PROJECTS\ESP32-S3-PhotoPainter\ESP32-S3-6Color-PhotoFrame'
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
idf.py build 2>&1 | Write-Host
