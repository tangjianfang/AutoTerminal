$bytes = [System.IO.File]::ReadAllBytes('C:\tjf\github\AutoTerminal\build\AutoTerminal.exe')
$text = [System.Text.Encoding]::ASCII.GetString($bytes)
$dlls = 'vcruntime140.dll','vcruntime140_app.dll','msvcp140.dll','msvcp140_2.dll','concrt140.dll'
foreach ($d in $dlls) {
    '{0,-25} present: {1}' -f $d, $text.Contains($d)
}