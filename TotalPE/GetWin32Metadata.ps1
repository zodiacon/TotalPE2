<#
.SYNOPSIS
Downloads the Win32 metadata (Windows.Win32.winmd), from which TotalPE's decompiler takes the prototypes of the functions of
Windows, their types and the names of their constants; and the WDK's (Windows.Wdk.winmd), of the kernel's.

.DESCRIPTION
The metadata is in the NuGet packages Microsoft.Windows.SDK.Win32Metadata and Microsoft.Windows.WDK.Win32Metadata, under the
license of the Windows SDK (it is put next to them, as sdk_license.txt and wdk_license.txt): it is not part of TotalPE's
sources. The packages are downloaded from nuget.org, and the metadata and its licenses are put in the Win32Metadata folder
of the solution, which the build copies next to TotalPE.exe and Win32ApiExp.exe (and, for the builds there are already,
next to them now). Without it, the decompiler works as it did: imports are called without their prototypes. Without the
WDK's, there are no functions and types of the kernel.

.PARAMETER Version
The version of the Win32 package.

.PARAMETER WdkVersion
The version of the WDK package (empty: it is not downloaded).

.EXAMPLE
.\GetWin32Metadata.ps1
#>
param(
	[string]$Version = "70.0.11-preview",
	[string]$WdkVersion = "0.13.25-experimental"
)
$ErrorActionPreference = "Stop"
$solution = Split-Path -Parent $PSScriptRoot
$folder = Join-Path $solution "Win32Metadata"
New-Item -ItemType Directory -Force $folder | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem

# a package's files: each one (its name in the package) to its name in the folder
function Get-Package([string]$package, [string]$version, [hashtable]$files) {
	$url = "https://api.nuget.org/v3-flatcontainer/$package/$version/$package.$version.nupkg"
	$archive = Join-Path $env:TEMP "$package.$version.zip"
	Write-Host "Downloading $url"
	Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing
	$zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
	try {
		foreach ($name in $files.Keys) {
			$entry = $zip.Entries | Where-Object { $_.FullName -eq $name } | Select-Object -First 1
			if (-not $entry) { throw "The package has no $name" }
			[System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $folder $files[$name]), $true)
		}
	}
	finally {
		$zip.Dispose()
		Remove-Item $archive -ErrorAction SilentlyContinue
	}
}

Get-Package "microsoft.windows.sdk.win32metadata" $Version @{ "Windows.Win32.winmd" = "Windows.Win32.winmd"; "sdk_license.txt" = "sdk_license.txt" }
$versions = "Win32: $Version"
if ($WdkVersion) {
	Get-Package "microsoft.windows.wdk.win32metadata" $WdkVersion @{ "Windows.Wdk.winmd" = "Windows.Wdk.winmd"; "sdk_license.txt" = "wdk_license.txt" }
	$versions += "`nWDK: $WdkVersion"
}
Set-Content (Join-Path $folder "version.txt") $versions

# next to the builds that are there
foreach ($build in Get-ChildItem (Join-Path $solution "x64") -Directory -ErrorAction SilentlyContinue) {
	if ((Test-Path (Join-Path $build.FullName "TotalPE.exe")) -or (Test-Path (Join-Path $build.FullName "Win32ApiExp.exe"))) {
		Copy-Item (Join-Path $folder "Windows.Win32.winmd") $build.FullName -Force
		Copy-Item (Join-Path $folder "sdk_license.txt") (Join-Path $build.FullName "Win32Metadata_license.txt") -Force
		if ($WdkVersion) {
			Copy-Item (Join-Path $folder "Windows.Wdk.winmd") $build.FullName -Force
			Copy-Item (Join-Path $folder "wdk_license.txt") (Join-Path $build.FullName "WdkMetadata_license.txt") -Force
		}
		Write-Host "Copied to $($build.FullName)"
	}
}
Write-Host "The metadata is in $folder ($($versions -replace "`n", ', '))"
