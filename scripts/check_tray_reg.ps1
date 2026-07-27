$paths = @(
  "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\TrayNotify",
  "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run",
  "HKCU:\Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\TrayNotify"
)
foreach ($p in $paths) {
  Write-Host "=== $p ==="
  if (Test-Path $p) {
    Get-ItemProperty $p -ErrorAction SilentlyContinue | Format-List *
  } else {
    Write-Host "  (key not present)"
  }
  Write-Host ""
}

# Look at Explorer startup approved for our autostart
Write-Host "=== AutoTerminal Run entries ==="
Get-ItemProperty "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -ErrorAction SilentlyContinue |
  Where-Object { $_.PSObject.Properties.Name -match "AutoTerminal" } |
  Format-List *