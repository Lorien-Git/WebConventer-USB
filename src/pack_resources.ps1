$ErrorActionPreference = "Stop"
$outCpp = "src/Resource.cpp"
$outH = "src/Resource.h"

$files = @(
    "frontend/index.html",
    "frontend/style.css",
    "frontend/script.js"
)

$headerContent = @"
#pragma once
#include <string_view>
#include <unordered_map>

struct ResourceData {
    const unsigned char* data;
    size_t size;
    const char* mime_type;
};

extern const std::unordered_map<std::string_view, ResourceData> g_Resources;
"@

Set-Content -Path $outH -Value $headerContent

$cppContent = @"
#include "Resource.h"

"@

foreach ($file in $files) {
    if (-not (Test-Path $file)) {
        Write-Warning "File not found: $file (this is normal during initial setup)"
        continue
    }
    
    $bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $file).Path)
    $varName = $file -replace '[/\\.]', '_'
    
    $hex = ($bytes | ForEach-Object { "0x{0:X2}" -f $_ }) -join ", "
    if ($bytes.Length -eq 0) {
        $hex = "0x00"
    }
    
    $cppContent += "static const unsigned char ${varName}[] = { $hex };`n"
}

$cppContent += @"

const std::unordered_map<std::string_view, ResourceData> g_Resources = {
"@

foreach ($file in $files) {
    if (-not (Test-Path $file)) {
        continue
    }
    
    $varName = $file -replace '[/\\.]', '_'
    $mimeType = "text/plain"
    $mappedPath = "/" + $file.Substring($file.IndexOf("/") + 1)
    if ($file -match '\.html$') { $mimeType = "text/html" }
    elseif ($file -match '\.css$') { $mimeType = "text/css" }
    elseif ($file -match '\.js$') { $mimeType = "application/javascript" }
    elseif ($file -match '\.exe$') { $mimeType = "application/octet-stream" }
    
    $cppContent += "    { `"$mappedPath`", { $varName, sizeof($varName), `"$mimeType`" } },`n"
}

# Add explicit root map to index.html
if (Test-Path "frontend/index.html") {
    $cppContent += "    { `"/`", { frontend_index_html, sizeof(frontend_index_html), `"text/html`" } },`n"
}

$cppContent += "};`n"

Set-Content -Path $outCpp -Value $cppContent
Write-Host "Resources packed successfully."
