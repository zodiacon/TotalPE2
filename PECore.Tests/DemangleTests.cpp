#include "TestCommon.h"
#include <ItaniumDemangle.h>
#include <fstream>

// what GNU c++filt prints for each
TEST_CASE("Itanium C++ names are demangled as c++filt does", "[demangle]") {
	std::pair<char const*, char const*> const cases[] = {
		{ "_Z3fooi", "foo(int)" },
		{ "_Z3foov", "foo()" },
		{ "_ZN3foo3barEv", "foo::bar()" },
		{ "_ZNK3foo3barEv", "foo::bar() const" },
		{ "_Z1fPKc", "f(char const*)" },
		{ "_Z1fRKSs", "f(std::string const&)" },
		{ "_ZNSsC1Ev", "std::basic_string<char, std::char_traits<char>, std::allocator<char> >::basic_string()" },
		{ "_ZN1AIiJEE1fEv", "A<int>::f()" },
		{ "_ZNSt6vectorIiSaIiEE9push_backERKi", "std::vector<int, std::allocator<int> >::push_back(int const&)" },
		{ "_ZN9QUrlQueryC2ERKS_", "QUrlQuery::QUrlQuery(QUrlQuery const&)" },
		{ "_ZN9QUrlQueryD1Ev", "QUrlQuery::~QUrlQuery()" },
		{ "_Z1fIiEvT_", "void f<int>(int)" },
		{ "_ZN1A1fIiEEvT_", "void A::f<int>(int)" },
		{ "_Z1fPFviE", "f(void (*)(int))" },
		{ "_Z1fM1AFviE", "f(void (A::*)(int))" },
		{ "_Z1fM1AKFviE", "f(void (A::*)(int) const)" },
		{ "_Z1fM1Ai", "f(int A::*)" },
		{ "_Z1fPA3_i", "f(int (*) [3])" },
		{ "_Z1fRA2_A3_i", "f(int (&) [2][3])" },
		{ "_Z1fz", "f(...)" },
		{ "_Z1fiz", "f(int, ...)" },
		{ "_ZNKSt8functionIFviEEclEi", "std::function<void (int)>::operator()(int) const" },
		{ "_ZN1AplERKS_", "A::operator+(A const&)" },
		{ "_ZN1AcviEv", "A::operator int()" },
		{ "_ZnwmPv", "operator new(unsigned long, void*)" },
		{ "_ZdlPv", "operator delete(void*)" },
		{ "_ZTV1A", "vtable for A" },
		{ "_ZTI1A", "typeinfo for A" },
		{ "_ZTS1A", "typeinfo name for A" },
		{ "_ZThn8_N1B1fEv", "non-virtual thunk to B::f()" },
		{ "_ZGVZ1fvE1x", "guard variable for f()::x" },
		{ "_ZZ1fvE1x", "f()::x" },
		{ "_ZN12_GLOBAL__N_13fooEv", "(anonymous namespace)::foo()" },
		{ "_ZL3barv", "bar()" },
		{ "_Z1fIJidEEvDpT_", "void f<int, double>(int, double)" },
		{ "_Z1fIJiRdEEvDpOT_", "void f<int, double&>(int&&, double&)" },
		{ "_Z1fIJEEvDpOT_", "void f<>()" },
		{ "_Z1fILi5EEvv", "void f<5>()" },
		{ "_Z1fILb1EEvv", "void f<true>()" },
		{ "_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6appendEPKc",
			"std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::append(char const*)" },
		{ "_ZN1A3fooB5cxx11Ev", "A::foo[abi:cxx11]()" },
		{ "_ZNSt8ios_base7failureB5cxx11D1Ev", "std::ios_base::failure[abi:cxx11]::~failure()" },
		{ "_ZZ1fIiEvT_ENKUlvE_clEv", "f<int>(int)::{lambda()#1}::operator()() const" },
		{ "_ZZ4mainENKUliE_clEi", "main::{lambda(int)#1}::operator()(int) const" },
		{ "_Z3foov.cold", "foo() [clone .cold]" },
		{ "_Z3fooi.isra.0", "foo(int) [clone .isra.0]" },
		{ "_ZNKR1A1fEv", "A::f() const &" },
		{ "_ZNO1A1fEv", "A::f() &&" },
		{ "_Z1fDn", "f(decltype(nullptr))" },
		{ "_Z1fOi", "f(int&&)" },
		{ "_Z1fIRKiEvOT_", "void f<int const&>(int const&)" },
		{ "_Z1fM1AKFbvES1_", "f(bool (A::*)() const, bool (A::*)() const)" },
		{ "_Z1fPVKi", "f(int const volatile*)" },
	};
	for (auto const& [mangled, expected] : cases) {
		INFO(mangled);
		CHECK(DemangleItanium(mangled) == expected);
	}
}

TEST_CASE("What is not a mangled name is left alone", "[demangle]") {
	CHECK(DemangleItanium("main").empty());
	CHECK(DemangleItanium("printf").empty());
	CHECK(DemangleItanium("_Z").empty());
	CHECK(DemangleItanium("_Zfoo").empty());
	CHECK(DemangleItanium("_Z3fo").empty());			// shorter than it says
	CHECK(DemangleItanium("_Z1fS5_").empty());			// a substitution that is not there
	CHECK(DemangleItanium("_Z1fT_").empty());			// a template parameter that is not there
	// deep nesting does not overflow the stack
	std::string deep = "_Z1f";
	for (int i = 0; i < 5000; i++)
		deep += "P";
	deep += "i";
	CHECK(DemangleItanium(deep).empty());
}

// A corpus of names and what c++filt says: TOTALPE_DEMANGLE_CORPUS=<file> (mangled TAB demangled per line)
TEST_CASE("Demangle a corpus", "[.demanglecorpus]") {
	char path[MAX_PATH];
	if (::GetEnvironmentVariableA("TOTALPE_DEMANGLE_CORPUS", path, MAX_PATH) == 0)
		SKIP("TOTALPE_DEMANGLE_CORPUS is not set");
	std::ifstream in(path);
	std::string line;
	size_t total = 0, same = 0, empty = 0, shown = 0;
	while (std::getline(in, line)) {
		auto tab = line.find('\t');
		if (tab == std::string::npos)
			continue;
		auto mangled = line.substr(0, tab), expected = line.substr(tab + 1);
		// the symbol versions of nm (foo@@VER, foo@VER) are not part of the names
		if (auto at = mangled.find('@'); at != std::string::npos) {
			auto version = mangled.substr(at);
			mangled.resize(at);
			if (expected.ends_with(version))
				expected.resize(expected.size() - version.size());
		}
		total++;
		auto result = DemangleItanium(mangled);
		if (result == expected)
			same++;
		else {
			if (result.empty())
				empty++;
			if (shown++ < 400)
				printf("%s\n  expected: %s\n  got:      %s\n", mangled.c_str(), expected.c_str(), result.c_str());
		}
	}
	printf("%zu of %zu the same (%.2f%%), %zu not demangled\n", same, total, total ? 100.0 * same / total : 0, empty);
}
