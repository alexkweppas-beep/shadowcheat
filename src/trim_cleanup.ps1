$path = "D:\антиграв\333333\444\676767\лоадер\src\cleanup.hpp"
$enc  = [System.Text.Encoding]::UTF8
$content = [System.IO.File]::ReadAllText($path, $enc)
$marker = "} // namespace shadow_cleanup"
$idx = $content.LastIndexOf($marker)
if ($idx -ge 0) {
    $trimmed = $content.Substring(0, $idx + $marker.Length) + "`r`n"
    [System.IO.File]::WriteAllText($path, $trimmed, $enc)
    Write-Host ("Done, kept up to index " + $idx)
} else {
    Write-Host "Marker not found"
}
