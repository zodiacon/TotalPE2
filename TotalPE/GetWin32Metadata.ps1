<#
.SYNOPSIS
Downloads the Win32 metadata (Windows.Win32.winmd), from which TotalPE's decompiler takes the prototypes of the functions of
Windows, their types and the names of their constants.

.DESCRIPTION
The metadata is in the NuGet package Microsoft.Windows.SDK.Win32Metadata, under the license of the Windows SDK (it is put
next to it, as sdk_license.txt): it is not part of TotalPE's sources. The package is downloaded from nuget.org, and the
metadata and its license are put in the Win32Metadata folder of the solution, which the build copies next to TotalPE.exe
(and, for the builds there are already, next to them now). Without it, the decompiler works as it did: imports are called
without their prototypes.

.PARAMETER Version
The version of the package.

.EXAMPLE
.\GetWin32Metadata.ps1
#>
param(
	[string]$Version = "70.0.11-preview"
)
$ErrorActionPreference = "Stop"
$solution = Split-Path -Parent $PSScriptRoot
$folder = Join-Path $solution "Win32Metadata"
$package = "microsoft.windows.sdk.win32metadata"
$url = "https://api.nuget.org/v3-flatcontainer/$package/$Version/$package.$Version.nupkg"

New-Item -ItemType Directory -Force $folder | Out-Null
$archive = Join-Path $env:TEMP "$package.$Version.zip"
Write-Host "Downloading $url"
Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
try {
	foreach ($name in "Windows.Win32.winmd", "sdk_license.txt") {
		$entry = $zip.Entries | Where-Object { $_.FullName -eq $name } | Select-Object -First 1
		if (-not $entry) { throw "The package has no $name" }
		[System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $folder $name), $true)
	}
}
finally {
	$zip.Dispose()
	Remove-Item $archive -ErrorAction SilentlyContinue
}
Set-Content (Join-Path $folder "version.txt") $Version

# next to the builds that are there
foreach ($build in Get-ChildItem (Join-Path $solution "x64") -Directory -ErrorAction SilentlyContinue) {
	if (Test-Path (Join-Path $build.FullName "TotalPE.exe")) {
		Copy-Item (Join-Path $folder "Windows.Win32.winmd") $build.FullName -Force
		Copy-Item (Join-Path $folder "sdk_license.txt") (Join-Path $build.FullName "Win32Metadata_license.txt") -Force
		Write-Host "Copied to $($build.FullName)"
	}
}
Write-Host "The Win32 metadata $Version is in $folder (its license: sdk_license.txt)"
