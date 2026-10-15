# Builds the file of signatures of library functions that TotalPE has (TotalPE\Signatures\msvc.sig): from the static libraries
# of the C runtime, vcruntime, the STL and ConcRT of the MSVC toolsets installed, and of the UCRT of the Windows SDKs installed
# (x64, x86, ARM64; release builds). Run it after building PECore.Tests (Release, x64).
param([string]$Out = "$PSScriptRoot\..\TotalPE\Signatures\msvc.sig", [string]$Tests = "$PSScriptRoot\..\x64\Release\PECore.Tests.exe")

$libs = @()
$names = 'libcmt.lib', 'libvcruntime.lib', 'libcpmt.lib', 'libconcrt.lib', 'msvcrt.lib', 'vcruntime.lib', 'msvcprt.lib'
foreach ($root in "$env:ProgramFiles\Microsoft Visual Studio", "${env:ProgramFiles(x86)}\Microsoft Visual Studio") {
	foreach ($msvc in Get-ChildItem "$root\*\*\VC\Tools\MSVC\*" -Directory -ErrorAction SilentlyContinue) {
		foreach ($arch in 'x64', 'x86', 'arm64') {
			foreach ($name in $names) {
				$lib = Join-Path $msvc.FullName "lib\$arch\$name"
				if (Test-Path $lib) { $libs += $lib }
			}
		}
	}
}
foreach ($sdk in Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Lib\*\ucrt" -Directory -ErrorAction SilentlyContinue) {
	foreach ($arch in 'x64', 'x86', 'arm64') {
		$lib = Join-Path $sdk.FullName "$arch\libucrt.lib"
		if (Test-Path $lib) { $libs += $lib }
	}
}
# (the same toolset in several places is one)
$libs = $libs | Sort-Object -Unique
"$($libs.Count) libraries"
New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null
$env:TOTALPE_SIG_OUT = [IO.Path]::GetFullPath($Out)
$env:TOTALPE_SIG_LIBS = $libs -join ';'
& $Tests "[sigbuild]" -s | Select-String "signatures:|warning" -Context 0,1 | ForEach-Object { $_.Context.PostContext } | Where-Object { $_ -match 'signatures:' }
Get-Item $Out | Select-Object Name, Length
