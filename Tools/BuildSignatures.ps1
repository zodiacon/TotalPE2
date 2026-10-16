# Builds the files of signatures of library functions that TotalPE has (TotalPE\Signatures\*.sig), from the static libraries
# installed (x64, x86, ARM64):
#   msvc.sig   the C runtime, vcruntime, the STL and ConcRT of the MSVC toolsets, the UCRT of the Windows SDKs (release
#              builds), and delay loading, COM support (_com_ptr_t, _bstr_t), the legacy stdio definitions
#   msvcd.sig  the same of debug builds (/MTd, /MDd)
#   atl.sig    ATL's static library (atls)
#   wdk.sig    of kernel drivers, from the WDKs: the kernel's C runtime (libcntpr), the stack cookie and GsDriverEntry
#              (BufferOverflowK, BufferOverflowFastFailK), ntstrsafe, wdmsec (IoCreateDeviceSecure), stdunk, aux_klib, and
#              KMDF's stub (WdfDriverEntry: FxDriverEntry, FxDriverEntryWorker...) of each version
# Run it after building PECore.Tests (Release, x64); -Only builds some of them ("wdk", "atl").
param([string]$Folder = "$PSScriptRoot\..\TotalPE\Signatures", [string]$Tests = "$PSScriptRoot\..\x64\Release\PECore.Tests.exe",
	[string[]]$Only = @())

$arches = 'x64', 'x86', 'arm64'
$toolsets = foreach ($root in "$env:ProgramFiles\Microsoft Visual Studio", "${env:ProgramFiles(x86)}\Microsoft Visual Studio") {
	Get-ChildItem "$root\*\*\VC\Tools\MSVC\*" -Directory -ErrorAction SilentlyContinue
}
$kits = "${env:ProgramFiles(x86)}\Windows Kits\10\Lib"

# the libraries of the toolsets (in lib\<arch>, or another folder of each), of the SDKs' UCRT, and of the WDKs
function Toolset([string[]]$names, [string]$sub = 'lib') {
	foreach ($msvc in $toolsets) { foreach ($arch in $arches) { foreach ($name in $names) {
		$lib = Join-Path $msvc.FullName "$sub\$arch\$name"
		if (Test-Path $lib) { $lib }
	} } }
}
function Ucrt([string]$name) {
	foreach ($sdk in Get-ChildItem "$kits\*\ucrt" -Directory -ErrorAction SilentlyContinue) { foreach ($arch in $arches) {
		$lib = Join-Path $sdk.FullName "$arch\$name"
		if (Test-Path $lib) { $lib }
	} }
}
function Kernel([string[]]$names) {
	foreach ($km in Get-ChildItem "$kits\*\km" -Directory -ErrorAction SilentlyContinue) { foreach ($arch in $arches) { foreach ($name in $names) {
		$lib = Join-Path $km.FullName "$arch\$name"
		if (Test-Path $lib) { $lib }
	} } }
	foreach ($arch in $arches) {
		Get-ChildItem "$kits\wdf\kmdf\$arch\*\WdfDriverEntry.lib" -ErrorAction SilentlyContinue | ForEach-Object FullName
	}
}

$sets = [ordered]@{
	msvc  = @(Toolset 'libcmt.lib', 'libvcruntime.lib', 'libcpmt.lib', 'libconcrt.lib', 'msvcrt.lib', 'vcruntime.lib', 'msvcprt.lib',
		'delayimp.lib', 'comsuppw.lib', 'comsupp.lib', 'legacy_stdio_definitions.lib') + @(Ucrt 'libucrt.lib')
	msvcd = @(Toolset 'libcmtd.lib', 'libvcruntimed.lib', 'libcpmtd.lib', 'libconcrtd.lib', 'msvcrtd.lib', 'vcruntimed.lib',
		'msvcprtd.lib', 'comsuppwd.lib', 'comsuppd.lib') + @(Ucrt 'libucrtd.lib')
	atl   = @(Toolset 'atls.lib' 'atlmfc\lib')
	wdk   = @(Kernel 'libcntpr.lib', 'BufferOverflowK.lib', 'BufferOverflowFastFailK.lib', 'ntstrsafe.lib', 'wdmsec.lib', 'stdunk.lib',
		'aux_klib.lib')
}
New-Item -ItemType Directory -Force $Folder | Out-Null
foreach ($name in $sets.Keys) {
	if ($Only.Count -and $Only -notcontains $name) { continue }
	# (the same toolset in several places is one)
	$libs = @($sets[$name] | Sort-Object -Unique)
	$out = [IO.Path]::GetFullPath((Join-Path $Folder "$name.sig"))
	"${name}: $($libs.Count) libraries"
	if (!$libs.Count) { continue }
	$env:TOTALPE_SIG_OUT = $out
	$env:TOTALPE_SIG_LIBS = $libs -join ';'
	$result = & $Tests "[sigbuild]" -s 2>&1
	$result | Select-String "signatures:" | Select-Object -Last 1 | ForEach-Object { "  " + $_.Line.Trim() }
	Get-Item $out -ErrorAction SilentlyContinue | ForEach-Object { "  {0}: {1:N0} bytes" -f $_.Name, $_.Length }
}
