// An explicit main (rather than Catch2Main.lib) because vcpkg auto-links every library in its lib directory, and more
// than one of them defines main.
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
	return Catch::Session().run(argc, argv);
}
