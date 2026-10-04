#include "pch.h"
#include "ItaniumDemangle.h"
#include <vector>

// The grammar is that of the Itanium C++ ABI (https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling); the output is
// formatted the way GNU c++filt does it ("char const*", "> >", "{lambda(int)#1}"...).

namespace {
	// A type as text. Types that wrap others need to know where to put themselves: a pointer to a function is
	// "void (*)(int)", not "void (int)*"; so a function or an array type keeps its parts apart.
	struct Type {
		std::string Left, Right;
		bool Function{ false }, Array{ false };

		std::string Str() const { return Left + Right; }
		static Type Of(std::string text) { return Type{ std::move(text) }; }
	};

	// what a name says about the function it names
	// the arguments of a pack are kept as one argument with this mark in front, separated by PackSeparator
	constexpr char PackMark = '\x01', PackSeparator = '\x03';
	// where an element of a pack goes in the pattern of a pack expansion (Dp)
	constexpr char PackElement = '\x02';

	std::vector<std::string> PackElements(std::string const& arg) {
		std::vector<std::string> elements;
		if (arg.size() <= 1)
			return elements;
		size_t start = 1;
		for (size_t i = 1; i <= arg.size(); i++) {
			if (i == arg.size() || arg[i] == PackSeparator) {
				elements.push_back(arg.substr(start, i - start));
				start = i + 1;
			}
		}
		return elements;
	}

	std::string Unmark(std::string const& arg) {
		if (arg.empty() || arg[0] != PackMark)
			return arg;
		std::string text;
		for (auto const& e : PackElements(arg))
			text += (text.empty() ? "" : ", ") + e;
		return text;
	}

	// a pattern with an element of a pack in it; a reference to a reference collapses ("T&" in "T&&" is "T&")
	std::string Instantiate(std::string const& pattern, std::string const& element) {
		auto at = pattern.find(PackElement);
		if (at == std::string::npos)
			return pattern;
		auto rest = pattern.substr(at + 1);
		auto e = element;
		int patternRef = rest.starts_with("&&") ? 2 : rest.starts_with("&") ? 1 : 0;
		// a reference to an array or a function has the reference inside: "char const (&) [14]"
		if (patternRef) {
			for (auto inner : { "(&&)", "(&)" }) {
				if (auto r = e.find(inner); r != std::string::npos) {
					if (patternRef == 1)
						e.replace(r, strlen(inner), "(&)");
					return Instantiate(pattern.substr(0, at) + e + rest.substr(patternRef), element);
				}
			}
		}
		int elementRef = e.ends_with("&&") ? 2 : e.ends_with("&") ? 1 : 0;
		if (elementRef && patternRef) {
			e.resize(e.size() - elementRef);
			rest = rest.substr(patternRef);
			e += elementRef == 1 || patternRef == 1 ? "&" : "&&";
		}
		return Instantiate(pattern.substr(0, at) + e + rest, element);
	}

	struct NameInfo {
		bool EndsWithTemplateArgs{ false };
		bool CtorDtorConversion{ false };	// no return type is mangled for these
		std::string Qualifiers;				// of a member function: " const", " &&"...
	};

	class Demangler {
	public:
		explicit Demangler(std::string_view text) : m_Text(text) {}

		std::string Demangle() {
			if (!Consume("_Z"))
				return {};
			auto result = Encoding();
			if (m_Failed)
				return {};
			// the clones GCC makes: ".cold", ".isra.0", ".constprop.0"...
			while (Peek() == '.' && (std::isalpha((unsigned char)Peek(1)) || Peek(1) == '_')) {
				size_t start = m_Pos++;
				while (std::isalnum((unsigned char)Peek()) || Peek() == '_')
					m_Pos++;
				while (Peek() == '.' && std::isdigit((unsigned char)Peek(1))) {
					m_Pos++;
					while (std::isdigit((unsigned char)Peek()))
						m_Pos++;
				}
				result += " [clone " + std::string(m_Text.substr(start, m_Pos - start)) + "]";
			}
			if (m_Pos != m_Text.size())
				return {};
			// what was not resolved (a pack outside of an expansion, say)
			for (char c : result)
				if (c == PackMark || c == PackElement || c == PackSeparator)
					return {};
			return result;
		}

	private:
		char Peek(size_t ahead = 0) const {
			return m_Pos + ahead < m_Text.size() ? m_Text[m_Pos + ahead] : '\0';
		}

		bool Consume(char c) {
			if (Peek() != c)
				return false;
			m_Pos++;
			return true;
		}

		bool Consume(std::string_view s) {
			if (m_Text.substr(m_Pos).starts_with(s)) {
				m_Pos += s.size();
				return true;
			}
			return false;
		}

		std::string Fail() {
			m_Failed = true;
			m_Pos = m_Text.size();
			return {};
		}

		Type FailType() {
			Fail();
			return {};
		}

		// guards against names that nest without end
		struct Depth {
			Demangler& D;
			explicit Depth(Demangler& d) : D(d) {
				if (++D.m_Depth > 200)
					D.Fail();
			}
			~Depth() { --D.m_Depth; }
		};

		bool Number(uint64_t& value) {
			if (!std::isdigit((unsigned char)Peek()))
				return false;
			value = 0;
			while (std::isdigit((unsigned char)Peek())) {
				value = value * 10 + (Peek() - '0');
				if (value > 1000000)
					return false;
				m_Pos++;
			}
			return true;
		}

		// <seq-id> in base 36, then '_'
		bool SeqId(size_t& value) {
			value = 0;
			while (std::isdigit((unsigned char)Peek()) || std::isupper((unsigned char)Peek())) {
				char c = Peek();
				value = value * 36 + (std::isdigit((unsigned char)c) ? c - '0' : c - 'A' + 10);
				m_Pos++;
			}
			return true;
		}

		// "<a, b>", with a space before a closing '>' that would make ">>" (but not after an empty pack, as GNU has it)
		static std::string TemplateArgsText(std::vector<std::string> const& args) {
			std::string text = "<";
			bool first = true;
			for (auto const& arg : args) {
				auto a = Unmark(arg);
				if (a.empty() && !arg.empty())
					continue;	// an empty pack
				if (!first)
					text += ", ";
				text += a;
				first = false;
			}
			bool emptyPackLast = !args.empty() && args.back().size() == 1 && args.back()[0] == PackMark;
			if (text.back() == '>' && !emptyPackLast)
				text += ' ';
			return text + ">";
		}

		std::string SourceName() {
			uint64_t length;
			if (!Number(length) || m_Pos + length > m_Text.size())
				return Fail();
			auto name = std::string(m_Text.substr(m_Pos, (size_t)length));
			m_Pos += (size_t)length;
			if (name.starts_with("_GLOBAL__N"))
				return "(anonymous namespace)";
			return name;
		}

		std::string AbiTags(std::string name) {
			while (Peek() == 'B') {
				m_Pos++;
				name += "[abi:" + SourceName() + "]";
			}
			return name;
		}

		static char const* OperatorName(std::string_view code) {
			static const std::pair<char const*, char const*> ops[] = {
				{ "nw", "new" }, { "na", "new[]" }, { "dl", "delete" }, { "da", "delete[]" }, { "ps", "+" }, { "ng", "-" }, { "ad", "&" },
				{ "de", "*" }, { "co", "~" }, { "pl", "+" }, { "mi", "-" }, { "ml", "*" }, { "dv", "/" }, { "rm", "%" }, { "an", "&" },
				{ "or", "|" }, { "eo", "^" }, { "aS", "=" }, { "pL", "+=" }, { "mI", "-=" }, { "mL", "*=" }, { "dV", "/=" }, { "rM", "%=" },
				{ "aN", "&=" }, { "oR", "|=" }, { "eO", "^=" }, { "ls", "<<" }, { "rs", ">>" }, { "lS", "<<=" }, { "rS", ">>=" },
				{ "eq", "==" }, { "ne", "!=" }, { "lt", "<" }, { "gt", ">" }, { "le", "<=" }, { "ge", ">=" }, { "ss", "<=>" },
				{ "nt", "!" }, { "aa", "&&" }, { "oo", "||" }, { "pp", "++" }, { "mm", "--" }, { "cm", "," }, { "pm", "->*" },
				{ "pt", "->" }, { "cl", "()" }, { "ix", "[]" }, { "qu", "?" }, { "aw", "co_await" },
			};
			for (auto const& [c, name] : ops)
				if (code == c)
					return name;
			return nullptr;
		}

		// <unqualified-name>: the last part of a name; 'scope' is the name of the class, for its constructors and destructor
		std::string UnqualifiedName(NameInfo& info, std::string const& scope) {
			Depth depth(*this);
			char c = Peek();
			std::string name;
			if (std::isdigit((unsigned char)c))
				name = SourceName();
			else if (c == 'L' && std::isdigit((unsigned char)Peek(1))) {	// internal linkage (GCC)
				m_Pos++;
				name = SourceName();
			}
			else if (c == 'C' && (std::isdigit((unsigned char)Peek(1)) || Peek(1) == 'I')) {
				m_Pos++;
				bool inheriting = Consume('I');
				m_Pos++;	// the kind (complete, base...)
				if (inheriting)
					ParseType();
				info.CtorDtorConversion = true;
				name = scope;
			}
			else if (c == 'D' && std::isdigit((unsigned char)Peek(1))) {
				m_Pos += 2;
				info.CtorDtorConversion = true;
				name = "~" + scope;
			}
			else if (c == 'U' && Peek(1) == 't') {
				m_Pos += 2;
				uint64_t n = 0;
				bool numbered = Number(n);
				if (!Consume('_'))
					return Fail();
				name = "{unnamed type#" + std::to_string(numbered ? n + 2 : 1) + "}";
			}
			else if (c == 'U' && Peek(1) == 'l') {
				m_Pos += 2;
				auto params = FunctionParameters('E');
				if (!Consume('E'))
					return Fail();
				uint64_t n = 0;
				bool numbered = Number(n);
				if (!Consume('_'))
					return Fail();
				name = "{lambda(" + params + ")#" + std::to_string(numbered ? n + 2 : 1) + "}";
			}
			else if (c == 'D' && Peek(1) == 'C') {		// a structured binding
				m_Pos += 2;
				name = "[";
				while (!Consume('E')) {
					if (name.size() > 1)
						name += ", ";
					name += SourceName();
					if (m_Failed)
						return {};
				}
				name += "]";
			}
			else if (c == 'c' && Peek(1) == 'v') {
				m_Pos += 2;
				info.CtorDtorConversion = true;
				name = "operator " + ParseType().Str();
			}
			else if (c == 'l' && Peek(1) == 'i') {
				m_Pos += 2;
				name = "operator\"\" " + SourceName();
			}
			else if (std::islower((unsigned char)c)) {
				auto op = OperatorName(m_Text.substr(m_Pos, 2));
				if (!op)
					return Fail();
				m_Pos += 2;
				name = std::string("operator") + (std::isalpha((unsigned char)op[0]) ? " " : "") + op;
			}
			else
				return Fail();
			return AbiTags(std::move(name));
		}

		// S_, S<seq-id>_ and the abbreviations of std; 'scope' gets the name a constructor of it would have.
		// In a prefix that a constructor or a destructor follows, std::string is spelled out (as GNU does).
		std::string Substitution(std::string& scope, bool prefix = false) {
			m_Pos++;	// 'S'
			char c = Peek();
			struct Abbreviation { char Code; char const* Name; char const* Full; char const* Scope; };
			static const Abbreviation abbreviations[] = {
				{ 'a', "std::allocator", "std::allocator", "allocator" },
				{ 'b', "std::basic_string", "std::basic_string", "basic_string" },
				{ 's', "std::string", "std::basic_string<char, std::char_traits<char>, std::allocator<char> >", "basic_string" },
				{ 'i', "std::istream", "std::basic_istream<char, std::char_traits<char> >", "basic_istream" },
				{ 'o', "std::ostream", "std::basic_ostream<char, std::char_traits<char> >", "basic_ostream" },
				{ 'd', "std::iostream", "std::basic_iostream<char, std::char_traits<char> >", "basic_iostream" },
			};
			for (auto const& a : abbreviations) {
				if (c == a.Code) {
					m_Pos++;
					scope = a.Scope;
					bool full = prefix && (Peek() == 'C' || Peek() == 'D');
					return full ? a.Full : a.Name;
				}
			}
			size_t index = 0;
			if (c != '_') {
				SeqId(index);
				index++;
			}
			if (!Consume('_') || index >= m_Subs.size())
				return Fail();
			auto text = m_Subs[index].Str();
			scope = LastComponent(text);
			return text;
		}

		// "ns::Foo<int>" -> "Foo"
		static std::string LastComponent(std::string const& name) {
			int angle = 0;
			size_t end = name.size(), start = 0;
			for (size_t i = name.size(); i-- > 0;) {
				char c = name[i];
				if (c == '>')
					angle++;
				else if (c == '<') {
					angle--;
					if (angle == 0)
						end = i;
				}
				else if (c == ':' && angle == 0 && i > 0 && name[i - 1] == ':') {
					start = i + 1;
					break;
				}
			}
			if (end < start)
				end = name.size();
			auto last = name.substr(start, end - start);
			if (auto tag = last.find("[abi:"); tag != std::string::npos)
				last.resize(tag);
			return last;
		}

		std::vector<std::string> TemplateArgs() {
			Depth depth(*this);
			m_Pos++;	// 'I'
			std::vector<std::string> args;
			while (!Consume('E')) {
				if (m_Failed || m_Pos >= m_Text.size())
					return {};
				args.push_back(TemplateArg());
			}
			return args;
		}

		std::string TemplateArg() {
			char c = Peek();
			if (c == 'L')
				return ExprPrimary();
			if (c == 'J') {		// a pack
				m_Pos++;
				std::string text(1, PackMark);
				bool first = true;
				while (!Consume('E')) {
					if (m_Failed || m_Pos >= m_Text.size())
						return Fail();
					if (!first)
						text += PackSeparator;
					text += Unmark(TemplateArg());
					first = false;
				}
				return text;
			}
			if (c == 'X')		// an expression
				return Fail();
			return ParseType().Str();
		}

		// L <type> <value> E, or L <mangled-name> E
		std::string ExprPrimary() {
			m_Pos++;	// 'L'
			if (Consume("_Z")) {
				auto name = Encoding();
				if (!Consume('E'))
					return Fail();
				return name;
			}
			auto type = ParseType().Str();
			bool negative = Consume('n');
			size_t start = m_Pos;
			// decimal, or lower case hex for floating point (the E that ends it is upper case)
			while (std::isdigit((unsigned char)Peek()) || std::islower((unsigned char)Peek()))
				m_Pos++;
			auto value = std::string(m_Text.substr(start, m_Pos - start));
			if (!Consume('E'))
				return Fail();
			if (negative)
				value = "-" + value;
			if (type == "bool")
				return value == "0" ? "false" : value == "1" ? "true" : "(bool)" + value;
			if (type == "int")
				return value;
			if (type == "unsigned int")
				return value + "u";
			if (type == "long")
				return value + "l";
			if (type == "unsigned long")
				return value + "ul";
			if (type == "long long")
				return value + "ll";
			if (type == "unsigned long long")
				return value + "ull";
			if (type == "decltype(nullptr)" && value.empty())
				return "(decltype(nullptr))0";
			return "(" + type + ")" + value;
		}

		Type TemplateParam() {
			m_Pos++;	// 'T'
			size_t index = 0;
			if (Peek() != '_') {
				uint64_t n;
				if (!Number(n))
					return FailType();
				index = (size_t)n + 1;
			}
			if (!Consume('_') || index >= m_TemplateArgs.size())
				return FailType();
			auto const& arg = m_TemplateArgs[index];
			if (!arg.empty() && arg[0] == PackMark) {
				// in a pack expansion, the pattern is made once for each element
				if (m_PackExpansion > 0 && m_Pack.empty()) {
					m_Pack = PackElements(arg);
					m_PackSeen = true;
					return Type::Of(std::string(1, PackElement));
				}
				m_ExpandedPack = true;
			}
			return Type::Of(Unmark(arg));
		}

		static std::string Qualifiers(bool restrict_, bool volatile_, bool const_) {
			std::string q;
			if (const_)
				q += " const";
			if (volatile_)
				q += " volatile";
			if (restrict_)
				q += " restrict";
			return q;
		}

		std::string CvQualifiers() {
			bool r = Consume('r'), v = Consume('V'), k = Consume('K');
			return Qualifiers(r, v, k);
		}

		// N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E
		std::string NestedName(NameInfo& info, bool top) {
			Depth depth(*this);
			m_Pos++;	// 'N'
			info.Qualifiers = CvQualifiers();
			if (Consume('R'))
				info.Qualifiers += " &";
			else if (Consume('O'))
				info.Qualifiers += " &&";
			std::string soFar, scope;
			while (!Consume('E')) {
				if (m_Failed || m_Pos >= m_Text.size())
					return Fail();
				char c = Peek();
				if (c == 'S' && Peek(1) == 't' && soFar.empty()) {
					m_Pos += 2;
					soFar = "std";
					continue;
				}
				if (c == 'S') {
					soFar = Substitution(scope, true);
					info.EndsWithTemplateArgs = false;
					continue;
				}
				if (c == 'T') {
					soFar = TemplateParam().Str();
					scope = LastComponent(soFar);
				}
				else if (c == 'I') {
					if (soFar.empty())
						return Fail();
					auto args = TemplateArgs();
					soFar += TemplateArgsText(args);
					info.EndsWithTemplateArgs = true;
					if (Peek() == 'E' && top)
						m_TemplateArgs = args;
				}
				else if (c == 'M') {	// the closure of a default member initializer
					m_Pos++;
					continue;
				}
				else {
					auto name = UnqualifiedName(info, scope);
					scope = LastComponent(name);
					soFar = soFar.empty() ? name : soFar + "::" + name;
					info.EndsWithTemplateArgs = false;
				}
				if (Peek() != 'E')
					m_Subs.push_back(Type::Of(soFar));
			}
			return soFar;
		}

		// Z <function encoding> E <entity name> [<discriminator>]
		std::string LocalName(NameInfo& info) {
			Depth depth(*this);
			m_Pos++;	// 'Z'
			auto saved = m_TemplateArgs;
			auto function = Encoding(false);
			m_TemplateArgs = saved;
			if (!Consume('E'))
				return Fail();
			std::string entity;
			if (Consume('s'))
				entity = "string literal";
			else {
				if (Consume('d')) {		// a default argument
					uint64_t n;
					Number(n);
					if (!Consume('_'))
						return Fail();
				}
				entity = Name(info, false);
			}
			// the discriminator: _<digit> or __<number>_
			if (Peek() == '_' && std::isdigit((unsigned char)Peek(1)))
				m_Pos += 2;
			else if (Peek() == '_' && Peek(1) == '_' && std::isdigit((unsigned char)Peek(2))) {
				m_Pos += 2;
				uint64_t n;
				Number(n);
				Consume('_');
			}
			return function + "::" + entity;
		}

		std::string Name(NameInfo& info, bool top) {
			Depth depth(*this);
			char c = Peek();
			if (c == 'N')
				return NestedName(info, top);
			if (c == 'Z')
				return LocalName(info);
			std::string name, scope;
			if (c == 'S' && Peek(1) == 't') {
				m_Pos += 2;
				name = "std::" + UnqualifiedName(info, scope);
			}
			else if (c == 'S') {
				name = Substitution(scope);
				if (Peek() != 'I')
					return Fail();
				auto args = TemplateArgs();
				if (top)
					m_TemplateArgs = args;
				info.EndsWithTemplateArgs = true;
				return name + TemplateArgsText(args);
			}
			else
				name = UnqualifiedName(info, scope);
			if (Peek() == 'I') {
				m_Subs.push_back(Type::Of(name));
				auto args = TemplateArgs();
				if (top)
					m_TemplateArgs = args;
				name += TemplateArgsText(args);
				info.EndsWithTemplateArgs = true;
			}
			return name;
		}

		std::string CallOffset() {
			if (Consume('h')) {
				uint64_t n;
				Consume('n');
				if (!Number(n) || !Consume('_'))
					return Fail();
			}
			else if (Consume('v')) {
				uint64_t n;
				Consume('n');
				if (!Number(n) || !Consume('_'))
					return Fail();
				Consume('n');
				if (!Number(n) || !Consume('_'))
					return Fail();
			}
			else
				return Fail();
			return {};
		}

		std::string SpecialName() {
			if (Consume("TV"))
				return "vtable for " + ParseType().Str();
			if (Consume("TT"))
				return "VTT for " + ParseType().Str();
			if (Consume("TI"))
				return "typeinfo for " + ParseType().Str();
			if (Consume("TS"))
				return "typeinfo name for " + ParseType().Str();
			if (Consume("TW")) {
				NameInfo info;
				return "TLS wrapper function for " + Name(info, false);
			}
			if (Consume("TH")) {
				NameInfo info;
				return "TLS init function for " + Name(info, false);
			}
			if (Consume("Th")) {
				m_Pos--;
				CallOffset();
				return "non-virtual thunk to " + Encoding();
			}
			if (Consume("Tv")) {
				m_Pos--;
				CallOffset();
				return "virtual thunk to " + Encoding();
			}
			if (Consume("Tc")) {
				CallOffset();
				CallOffset();
				return "covariant return thunk to " + Encoding();
			}
			if (Consume("TC")) {
				auto derived = ParseType().Str();
				uint64_t n;
				if (!Number(n) || !Consume('_'))
					return Fail();
				auto base = ParseType().Str();
				return "construction vtable for " + base + "-in-" + derived;
			}
			if (Consume("GV")) {
				NameInfo info;
				return "guard variable for " + Name(info, false);
			}
			if (Consume("GR")) {
				NameInfo info;
				auto name = Name(info, false);
				size_t id = 0;
				if (Peek() != '_') {
					SeqId(id);
					id++;
				}
				if (!Consume('_'))
					return Fail();
				return "reference temporary #" + std::to_string(id) + " for " + name;
			}
			if (Consume("GTt"))
				return "transaction clone for " + Encoding();
			return Fail();
		}

		// the types of the parameters, up to 'end' (or the end of the name): "" for (void)
		std::string FunctionParameters(char end) {
			std::vector<std::string> params;
			while (m_Pos < m_Text.size() && Peek() != end && Peek() != '.' && !(end == 'E' && (Peek() == 'R' || Peek() == 'O') && Peek(1) == 'E')) {
				if (m_Failed)
					return {};
				params.push_back(ParseType().Str());
			}
			if (params.size() == 1 && params[0] == "void")
				return {};
			// an empty pack is no parameter
			std::string text;
			for (auto const& p : params)
				if (!p.empty() && p != std::string(1, PackMark))
					text += (text.empty() ? "" : ", ") + p;
			return text;
		}

		std::string Encoding(bool returnType = true) {
			Depth depth(*this);
			char c = Peek();
			if (c == 'T' || (c == 'G' && (Peek(1) == 'V' || Peek(1) == 'R' || Peek(1) == 'T')))
				return SpecialName();
			NameInfo info;
			auto name = Name(info, true);
			if (m_Failed)
				return {};
			// a variable, or the function of a local name
			if (m_Pos >= m_Text.size() || Peek() == 'E' || Peek() == '.')
				return name;
			std::string ret;
			if (info.EndsWithTemplateArgs && !info.CtorDtorConversion) {
				ret = ParseType().Str() + " ";
				if (!returnType)
					ret.clear();
			}
			auto params = FunctionParameters('E');
			return ret + name + "(" + params + ")" + info.Qualifiers;
		}

		Type Builtin(char c) {
			switch (c) {
				case 'v': return Type::Of("void");
				case 'w': return Type::Of("wchar_t");
				case 'b': return Type::Of("bool");
				case 'c': return Type::Of("char");
				case 'a': return Type::Of("signed char");
				case 'h': return Type::Of("unsigned char");
				case 's': return Type::Of("short");
				case 't': return Type::Of("unsigned short");
				case 'i': return Type::Of("int");
				case 'j': return Type::Of("unsigned int");
				case 'l': return Type::Of("long");
				case 'm': return Type::Of("unsigned long");
				case 'x': return Type::Of("long long");
				case 'y': return Type::Of("unsigned long long");
				case 'n': return Type::Of("__int128");
				case 'o': return Type::Of("unsigned __int128");
				case 'f': return Type::Of("float");
				case 'd': return Type::Of("double");
				case 'e': return Type::Of("long double");
				case 'g': return Type::Of("__float128");
				case 'z': return Type::Of("...");
			}
			return {};
		}

		// a type that wraps another: a pointer, a reference
		static Type Wrap(Type const& inner, char const* symbol) {
			Type t;
			if (inner.Function) {
				t.Left = inner.Left + "(" + symbol;
				t.Right = ")" + inner.Right;
			}
			else if (inner.Array) {
				t.Left = inner.Left + "(" + symbol;
				t.Right = ") " + inner.Right;
			}
			else {
				auto left = inner.Left;
				std::string_view s(symbol);
				if (s[0] == '&' && left.ends_with('&')) {
					bool lvalue = !left.ends_with("&&") || s == "&";
					left.resize(left.size() - (left.ends_with("&&") ? 2 : 1));
					t.Left = left + (lvalue ? "&" : "&&") + inner.Right;
				}
				else
					t.Left = left + symbol + inner.Right;
			}
			return t;
		}

		// F [Y] <return type> <parameter types> [<ref-qualifier>] E (not a substitution candidate by itself: the caller adds it)
		Type FunctionType() {
			m_Pos++;	// 'F'
			Consume('Y');
			auto ret = ParseType().Str();
			auto params = FunctionParameters('E');
			std::string ref;
			if (Consume('R'))
				ref = " &";
			else if (Consume('O'))
				ref = " &&";
			if (!Consume('E'))
				return FailType();
			Type t;
			t.Left = ret + " ";
			t.Right = "(" + params + ")" + ref;
			t.Function = true;
			return t;
		}

		Type ParseType() {
			Depth depth(*this);
			if (m_Failed)
				return {};
			char c = Peek();
			if (auto builtin = Builtin(c); !builtin.Left.empty()) {
				m_Pos++;
				return builtin;
			}
			Type t;
			switch (c) {
				case 'r': case 'V': case 'K':
				{
					auto q = CvQualifiers();
					if (Peek() == 'F') {
						t = FunctionType();
						t.Right += q;
						break;
					}
					auto inner = ParseType();
					t = inner;
					if (inner.Function)
						t.Right += q;
					else
						t.Left = inner.Left + q;
					break;
				}
				case 'P': m_Pos++; t = Wrap(ParseType(), "*"); break;
				case 'R': m_Pos++; t = Wrap(ParseType(), "&"); break;
				case 'O': m_Pos++; t = Wrap(ParseType(), "&&"); break;
				case 'F':
					t = FunctionType();
					break;
				case 'A':
				{
					m_Pos++;
					std::string size;
					uint64_t n;
					if (Number(n))
						size = std::to_string(n);
					else if (Peek() != '_')
						return FailType();
					if (!Consume('_'))
						return FailType();
					auto element = ParseType();
					if (element.Array) {
						t.Left = element.Left;
						t.Right = "[" + size + "]" + element.Right;
					}
					else {
						t.Left = element.Str() + " ";
						t.Right = "[" + size + "]";
					}
					t.Array = true;
					break;
				}
				case 'M':
				{
					m_Pos++;
					auto cls = ParseType().Str();
					auto member = ParseType();
					if (member.Function) {
						t.Left = member.Left + "(" + cls + "::*";
						t.Right = ")" + member.Right;
					}
					else
						t.Left = member.Str() + " " + cls + "::*";
					break;
				}
				case 'T':
				{
					t = TemplateParam();
					if (Peek() == 'I') {		// a template template parameter with its arguments
						m_Subs.push_back(t);
						t = Type::Of(t.Str() + TemplateArgsText(TemplateArgs()));
					}
					break;
				}
				case 'S':
				{
					if (Peek(1) == 't') {
						NameInfo info;
						t = Type::Of(Name(info, false));
						break;
					}
					std::string scope;
					auto sub = Substitution(scope);
					if (Peek() != 'I')
						return Type::Of(sub);	// a substitution is not a new candidate
					t = Type::Of(sub + TemplateArgsText(TemplateArgs()));
					break;
				}
				case 'D':
				{
					char d = Peek(1);
					m_Pos += 2;
					switch (d) {
						case 'p':
						{
							// a pack that is known: the pattern for each of its types; one that is not: "T..."
							m_ExpandedPack = false;
							auto savedPack = std::move(m_Pack);
							bool savedSeen = m_PackSeen;
							m_Pack.clear();
							m_PackSeen = false;
							m_PackExpansion++;
							t = ParseType();
							m_PackExpansion--;
							if (m_PackSeen) {
								auto pattern = t.Str();
								std::string text(1, PackMark);
								for (size_t i = 0; i < m_Pack.size(); i++)
									text += (i ? std::string(1, PackSeparator) : "") + Instantiate(pattern, m_Pack[i]);
								t = Type::Of(Unmark(text));
								if (m_Pack.empty())
									t = Type::Of(std::string(1, PackMark));	// an empty pack: nothing
							}
							else if (!m_ExpandedPack)
								t.Left += "...";
							m_Pack = std::move(savedPack);
							m_PackSeen = savedSeen;
							m_ExpandedPack = false;
							break;
						}
						case 'n': return Type::Of("decltype(nullptr)");
						case 'a': return Type::Of("auto");
						case 'c': return Type::Of("decltype(auto)");
						case 'i': return Type::Of("char32_t");
						case 's': return Type::Of("char16_t");
						case 'u': return Type::Of("char8_t");
						case 'f': return Type::Of("decimal32");
						case 'd': return Type::Of("decimal64");
						case 'e': return Type::Of("decimal128");
						case 'h': return Type::Of("half");
						case 'F':
						{
							uint64_t bits;
							if (!Number(bits))
								return FailType();
							auto name = "_Float" + std::to_string(bits);
							if (Consume('x'))
								name += "x";
							else if (!Consume('_'))
								return FailType();
							return Type::Of(name);
						}
						default: return FailType();
					}
					break;
				}
				case 'u':
					m_Pos++;
					return Type::Of(SourceName());
				case 'N': case 'Z':
				default:
				{
					if (!(c == 'N' || c == 'Z' || std::isdigit((unsigned char)c) || (c == 'L' && std::isdigit((unsigned char)Peek(1)))
						|| (c == 'U' && (Peek(1) == 't' || Peek(1) == 'l'))))
						return FailType();
					NameInfo info;
					t = Type::Of(Name(info, false));
					break;
				}
			}
			if (m_Failed)
				return {};
			m_Subs.push_back(t);
			return t;
		}

		std::string_view m_Text;
		size_t m_Pos{ 0 };
		bool m_Failed{ false };
		int m_Depth{ 0 };
		std::vector<Type> m_Subs;
		std::vector<std::string> m_TemplateArgs;
		bool m_ExpandedPack{ false };
		int m_PackExpansion{ 0 };			// in Dp
		std::vector<std::string> m_Pack;	// the elements of the pack that the expansion is for
		bool m_PackSeen{ false };
	};
}

std::string DemangleItanium(std::string_view mangled) {
	if (!mangled.starts_with("_Z") || mangled.size() > 4096)
		return {};
	return Demangler(mangled).Demangle();
}
