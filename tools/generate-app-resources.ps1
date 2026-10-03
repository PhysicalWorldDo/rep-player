$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot
$resourcePath = Join-Path $taskRoot 'build\resources'
New-Item -ItemType Directory -Path $resourcePath -Force | Out-Null

# Keep only display/search data. Development paths and provenance stay in source.
$map = Get-Content -LiteralPath (Join-Path $taskRoot 'ui_design\data\skill-name-map.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$fields = @('relativePath','job','jobZh','english','zh','vpName','displayZh','displayEn','aliases')
$entries = @(foreach ($entry in $map.entries) {
    $row = [ordered]@{}
    foreach ($field in $fields) { $row[$field] = $entry.$field }
    $row
})
$json = [ordered]@{ entries = $entries } | ConvertTo-Json -Depth 8 -Compress
$utf8 = New-Object System.Text.UTF8Encoding $false
[IO.File]::WriteAllText((Join-Path $resourcePath 'skill-names.json'), $json, $utf8)

# The compiled registry is authoritative; include every registered VS/PS bytecode.
$registry = Get-Content -LiteralPath (Join-Path $taskRoot 'src\shader_registry.hpp') -Raw
$shaders = @([regex]::Matches($registry, '[a-f0-9]+\.(?:vs|ps)\.dxbc') | ForEach-Object { $_.Value } | Sort-Object -Unique)
$lines = @('SKILL_NAME_MAP RCDATA "../build/resources/skill-names.json"')
foreach ($shader in $shaders) {
    if (!(Test-Path -LiteralPath (Join-Path $taskRoot "assets\shaders\$shader"))) { throw "Registered shader missing: $shader" }
    $lines += 'SHADER_' + $shader.ToUpperInvariant().Replace('.', '_') + ' RCDATA "../assets/shaders/' + $shader + '"'
}
[IO.File]::WriteAllText((Join-Path $resourcePath 'app-resources.rc'), ($lines -join "`r`n") + "`r`n", $utf8)
Write-Output "Generated embedded resources: $($entries.Count) skill names, $($shaders.Count) shaders."
