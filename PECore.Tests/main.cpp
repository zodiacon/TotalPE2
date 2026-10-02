// An explicit main (rather than Catch2Main.lib) because vcpkg auto-links every library in its lib
// directory, and more than one of them defines main.
#include <catch2/catch_session.hpp>
#include <crtdbg.h>

int main(int argc, char* argv[]) {
#ifdef _DEBUG
	// LIEF calls isprint() with negative char values when it validates names read from damaged files. The debug CRT
	// treats that as an assertion failure and aborts the process, the release CRT accepts it. Report such assertions
	// on stderr and carry on, so that the robustness tests exercise the rest of the parser.
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
	return Catch::Session().run(argc, argv);
}
