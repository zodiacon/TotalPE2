#include "TestCommon.h"
#include <InfSyntax.h>

namespace {
	// the styles of a line as letters, one per character: . default, ; comment, [ section, k key, = operator, " string,
	// % string key, 0 number, H root key
	std::string Styles(std::string_view line) {
		std::string result;
		for (auto s : InfLineStyles(line))
			result += ".;[k=\"%0H"[(int)s];
		return result;
	}
}

TEST_CASE("Sections and comments of INF files", "[inf]") {
	CHECK(Styles("[Version]") == "[[[[[[[[[");
	CHECK(Styles("  [Reg] ; c") == "..[[[[[.;;;");
	CHECK(Styles("; a comment") == ";;;;;;;;;;;");
	CHECK(Styles("") == "");
	CHECK(Styles("   ") == "...");
	CHECK(Styles("[Unclosed") == "[[[[[[[[[");
}

TEST_CASE("Keys, values and strings of INF files", "[inf]") {
	CHECK(Styles("AddReg=Reg.X") == "kkkkkk=.....");
	CHECK(Styles("Key = 1") == "kkk.=.0");
	CHECK(Styles("Signature=\"$CHICAGO$\"") == "kkkkkkkkk=\"\"\"\"\"\"\"\"\"\"\"");
	CHECK(Styles("AdvOptions=0x24 ;x") == "kkkkkkkkkk=0000.;;");
	CHECK(Styles("RequiredEngine=SETUPAPI,%ERR%") == "kkkkkkkkkkkkkk=........=%%%%%");
	// "" is a quote in a string; a string key in a string
	CHECK(Styles("\"a\"\"b\",1") == "\"\"\"\"\"\"=0");
	CHECK(Styles("\"%_MOD%\\x\"") == "\"%%%%%%\"\"\"");
	// a lone % is not a string key; ; in a string is not a comment
	CHECK(Styles("\"5%\";c") == "\"\"\"\";;");
	// with the line break
	CHECK(Styles("A=1\r\n") == "k=0..");
}

TEST_CASE("Registry lines of INF files", "[inf]") {
	CHECK(Styles("HKLM,\"Software\",\"N\",0,\"\"") == "HHHH=\"\"\"\"\"\"\"\"\"\"=\"\"\"=0=\"\"");
	CHECK(Styles("hkcr,CLSID") == "HHHH=.....");
	// a root key only as the first field
	CHECK(Styles("X,HKLM") == ".=....");
	CHECK(Styles("HKLMX,1") == ".....=0");
}
