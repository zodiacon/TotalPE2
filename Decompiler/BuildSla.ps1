<#
.SYNOPSIS
	Compiles the decompiler's .sla files again, with the SLEIGH compiler of the same version of Ghidra.

.DESCRIPTION
	A .sla file is a processor's SLEIGH specification (a .slaspec and its .sinc files) compiled by Ghidra's SLEIGH
	compiler, and its format belongs to the decompiler's version. This script:

	  1. gets the sources of that version of Ghidra (a sparse clone of the tag: the decompiler's sources and the
	     languages of the processors), unless a checkout is given with -Source;
	  2. builds the SLEIGH compiler (sleigh.exe) with Visual Studio's compiler: the CORE, SLEIGH and SLACOMP files of the
	     decompiler's Makefile, with zlib;
	  3. compiles x86.slaspec, x86-64.slaspec and AARCH64.slaspec;
	  4. puts the .sla files in languages\x86 and languages\AARCH64 (or, with -Check, only compares them with those there).

	With -Definitions, the language definitions that are in languages (.ldefs, .pspec, .cspec) are copied from the same
	version as well (after an update of the sources).

.PARAMETER Tag
	The Ghidra tag (the version in README.md).

.PARAMETER Source
	A checkout of Ghidra's sources (of the tag) to use, instead of cloning one.

.PARAMETER WorkFolder
	Where the clone, the compiler and the compiled files go. Kept, so that the next run need not clone or build again.

.PARAMETER Check
	Compile, and tell whether the .sla files in languages are the same; nothing is changed. The exit code is 1 if not.

.PARAMETER Definitions
	Also copy the language definitions in languages from the sources.

.EXAMPLE
	.\BuildSla.ps1 -Check

.EXAMPLE
	.\BuildSla.ps1 -Tag Ghidra_12.2_build -Definitions
#>
[CmdletBinding()]
param(
	[string]$Tag = 'Ghidra_12.1.4_build',
	[string]$Source,
	[string]$WorkFolder = (Join-Path ([IO.Path]::GetTempPath()) 'TotalPE-sla'),
	[switch]$Check,
	[switch]$Definitions
)
$ErrorActionPreference = 'Stop'

$languages = Join-Path $PSScriptRoot 'languages'
# processor folder in Ghidra -> folder in languages, and the specifications compiled there
$processors = @(
	@{ Processor = 'x86'; Folder = 'x86'; Specs = @('x86', 'x86-64') },
	@{ Processor = 'AARCH64'; Folder = 'AARCH64'; Specs = @('AARCH64') }
)
$decompile = 'Ghidra/Features/Decompiler/src/decompile'

function Step($text) { Write-Host "== $text" -ForegroundColor Cyan }

# the git command, failing on an error (git writes progress to stderr, which is not one)
function Invoke-Git {
	$saved = $ErrorActionPreference
	$ErrorActionPreference = 'Continue'
	& git.exe @args 2>&1 | ForEach-Object { Write-Verbose "$_" }
	$code = $LASTEXITCODE
	$ErrorActionPreference = $saved
	if ($code -ne 0) { throw "git $($args -join ' ') failed ($code)" }
}

New-Item -ItemType Directory -Force $WorkFolder | Out-Null

# 1. the sources
if ($Source) {
	$ghidra = (Resolve-Path $Source).Path
}
else {
	$ghidra = Join-Path $WorkFolder "ghidra-$Tag"
	$sparse = @("/$decompile/cpp/", "/$decompile/zlib/") + ($processors | ForEach-Object { "/Ghidra/Processors/$($_.Processor)/data/languages/" })
	if (-not (Test-Path (Join-Path $ghidra '.git'))) {
		Step "Cloning Ghidra ($Tag) into $ghidra"
		Invoke-Git clone --quiet --depth 1 --branch $Tag --filter=blob:none --sparse https://github.com/NationalSecurityAgency/ghidra.git $ghidra
	}
	Step 'Checking out the sources'
	Invoke-Git -C $ghidra sparse-checkout set --no-cone @sparse
}
$cpp = Join-Path $ghidra "$decompile/cpp"
$zlib = Join-Path $ghidra "$decompile/zlib"
if (-not (Test-Path (Join-Path $cpp 'slgh_compile.cc'))) { throw "The decompiler's sources are not in $ghidra" }

# 2. the compiler: the files of CORE, SLEIGH and SLACOMP in the Makefile (a list may go on over lines that end with '\')
$makefile = (Get-Content (Join-Path $cpp 'Makefile') -Raw) -replace '\\\r?\n', ' '
$names = foreach ($list in 'CORE', 'SLEIGH', 'SLACOMP') {
	if ($makefile -notmatch "(?m)^$list\s*=(.*)$") { throw "The Makefile has no $list" }
	$Matches[1].Trim() -split '\s+'
}
$sleighFolder = Join-Path $WorkFolder "sleigh-$Tag"
$sleigh = Join-Path $sleighFolder 'sleigh.exe'
if (-not (Test-Path $sleigh)) {
	Step 'Building the SLEIGH compiler'
	$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	if (-not (Test-Path $vswhere)) { throw 'Visual Studio is not installed (no vswhere.exe)' }
	$vs = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
	if (-not $vs) { throw 'No Visual Studio with the C++ tools' }
	$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
	New-Item -ItemType Directory -Force $sleighFolder | Out-Null
	$cc = ($names | ForEach-Object { "`"$(Join-Path $cpp "$_.cc")`"" }) -join ' '
	$c = (Get-ChildItem $zlib -Filter *.c | ForEach-Object { "`"$($_.FullName)`"" }) -join ' '
	# as the decompiler is built (Decompiler.vcxproj)
	$defines = '/DWINDOWS /D_WINDOWS /DWIN32 /D_WIN32 /DWIN64 /D_WIN64 /DLOCAL_ZLIB /DNO_GZIP /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE'
	$build = @"
@echo off
call "$vcvars" >nul 2>&1 || exit /b 1
cd /d "$sleighFolder"
cl /nologo /c /MP /O2 /W0 $defines $c >zlib.log || (type zlib.log & exit /b 1)
cl /nologo /c /MP /O2 /W0 /EHsc /std:c++17 $defines /I"$zlib" $cc >sleigh.log || (type sleigh.log & exit /b 1)
link /nologo /OUT:sleigh.exe *.obj >link.log || (type link.log & exit /b 1)
"@
	$cmd = Join-Path $sleighFolder 'build.cmd'
	Set-Content $cmd $build -Encoding Ascii
	& cmd.exe /c $cmd
	if ($LASTEXITCODE -ne 0 -or -not (Test-Path $sleigh)) { throw 'The SLEIGH compiler did not build' }
}

# 3. the specifications
$out = Join-Path $WorkFolder "sla-$Tag"
New-Item -ItemType Directory -Force $out | Out-Null
$compiled = @()
foreach ($p in $processors) {
	$data = Join-Path $ghidra "Ghidra/Processors/$($p.Processor)/data/languages"
	foreach ($spec in $p.Specs) {
		Step "Compiling $spec.slaspec"
		$sla = Join-Path $out "$spec.sla"
		Remove-Item $sla -ErrorAction SilentlyContinue
		# the compiler finds the .sinc files of the specification next to it
		Push-Location $data
		try { & $sleigh "$spec.slaspec" $sla | ForEach-Object { Write-Verbose "$_" } }
		finally { Pop-Location }
		if ($LASTEXITCODE -ne 0 -or -not (Test-Path $sla)) { throw "$spec.slaspec did not compile" }
		$compiled += @{ File = $sla; Target = Join-Path $languages "$($p.Folder)\$spec.sla" }
	}
}

# 4. into languages, or compared with what is there
$different = 0
foreach ($f in $compiled) {
	$name = Split-Path -Leaf $f.Target
	$same = (Test-Path $f.Target) -and (Get-FileHash $f.File).Hash -eq (Get-FileHash $f.Target).Hash
	if ($Check) {
		if ($same) { Write-Host "$name is the same" }
		else { Write-Host "$name is different" -ForegroundColor Yellow; $different++ }
	}
	elseif ($same) {
		Write-Host "$name has not changed"
	}
	else {
		Copy-Item $f.File $f.Target -Force
		Write-Host "$name is updated" -ForegroundColor Green
	}
}
if ($Definitions) {
	foreach ($p in $processors) {
		$data = Join-Path $ghidra "Ghidra/Processors/$($p.Processor)/data/languages"
		foreach ($file in Get-ChildItem (Join-Path $languages $p.Folder) -Include *.ldefs, *.pspec, *.cspec -Recurse) {
			$from = Join-Path $data $file.Name
			if (-not (Test-Path $from)) { Write-Warning "$($file.Name) is not in $Tag"; continue }
			if ((Get-FileHash $from).Hash -eq (Get-FileHash $file.FullName).Hash) { continue }
			if ($Check) { Write-Host "$($file.Name) is different" -ForegroundColor Yellow; $different++ }
			else { Copy-Item $from $file.FullName -Force; Write-Host "$($file.Name) is updated" -ForegroundColor Green }
		}
	}
}
if ($Check -and $different) { exit 1 }
