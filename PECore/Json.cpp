#include "pch.h"
#include "Json.h"
#include <charconv>

namespace {
	struct Parser {
		std::string_view Text;
		size_t Pos{ 0 };
		std::string Error;
		static constexpr int MaxDepth = 128;	// (the stack of a debug build holds this many, with room to spare)

		bool Fail(char const* what) {
			if (Error.empty())
				Error = std::format("{} at {}", what, Pos);
			return false;
		}
		void Space() {
			while (Pos < Text.size() && (Text[Pos] == ' ' || Text[Pos] == '\t' || Text[Pos] == '\r' || Text[Pos] == '\n'))
				Pos++;
		}
		bool Literal(std::string_view word) {
			if (Text.substr(Pos, word.size()) != word)
				return false;
			Pos += word.size();
			return true;
		}
		static void AppendUtf8(std::string& out, uint32_t cp) {
			if (cp < 0x80)
				out += (char)cp;
			else if (cp < 0x800) {
				out += (char)(0xC0 | (cp >> 6));
				out += (char)(0x80 | (cp & 0x3F));
			}
			else if (cp < 0x10000) {
				out += (char)(0xE0 | (cp >> 12));
				out += (char)(0x80 | ((cp >> 6) & 0x3F));
				out += (char)(0x80 | (cp & 0x3F));
			}
			else {
				out += (char)(0xF0 | (cp >> 18));
				out += (char)(0x80 | ((cp >> 12) & 0x3F));
				out += (char)(0x80 | ((cp >> 6) & 0x3F));
				out += (char)(0x80 | (cp & 0x3F));
			}
		}
		bool Hex4(uint32_t& value) {
			if (Pos + 4 > Text.size())
				return false;
			value = 0;
			for (int i = 0; i < 4; i++) {
				char c = Text[Pos++];
				value <<= 4;
				if (c >= '0' && c <= '9')
					value |= c - '0';
				else if (c >= 'a' && c <= 'f')
					value |= c - 'a' + 10;
				else if (c >= 'A' && c <= 'F')
					value |= c - 'A' + 10;
				else
					return false;
			}
			return true;
		}
		bool String(std::string& out) {
			Pos++;	// "
			for (;;) {
				if (Pos >= Text.size())
					return Fail("A string does not end");
				char c = Text[Pos++];
				if (c == '"')
					return true;
				if ((unsigned char)c < 0x20)
					return Fail("A control character in a string");
				if (c != '\\') {
					out += c;
					continue;
				}
				if (Pos >= Text.size())
					return Fail("A string does not end");
				switch (Text[Pos++]) {
					case '"': out += '"'; break;
					case '\\': out += '\\'; break;
					case '/': out += '/'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case 'n': out += '\n'; break;
					case 'r': out += '\r'; break;
					case 't': out += '\t'; break;
					case 'u':
					{
						uint32_t cp;
						if (!Hex4(cp))
							return Fail("A bad \\u escape");
						// a surrogate pair is one character
						if (cp >= 0xD800 && cp < 0xDC00 && Text.substr(Pos, 2) == "\\u") {
							auto save = Pos;
							Pos += 2;
							uint32_t low;
							if (Hex4(low) && low >= 0xDC00 && low < 0xE000)
								cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
							else
								Pos = save;
						}
						AppendUtf8(out, cp);
						break;
					}
					default:
						return Fail("A bad escape");
				}
			}
		}
		bool Value(JsonValue& value, int depth) {
			if (depth > MaxDepth)
				return Fail("Too deep");
			Space();
			if (Pos >= Text.size())
				return Fail("A value is missing");
			char c = Text[Pos];
			if (c == '{') {
				Pos++;
				JsonValue::Object object;
				Space();
				if (Pos < Text.size() && Text[Pos] == '}') {
					Pos++;
					value = JsonValue(std::move(object));
					return true;
				}
				for (;;) {
					Space();
					if (Pos >= Text.size() || Text[Pos] != '"')
						return Fail("A name is expected");
					std::string name;
					if (!String(name))
						return false;
					Space();
					if (Pos >= Text.size() || Text[Pos] != ':')
						return Fail("':' is expected");
					Pos++;
					JsonValue member;
					if (!Value(member, depth + 1))
						return false;
					object[std::move(name)] = std::move(member);
					Space();
					if (Pos < Text.size() && Text[Pos] == ',') {
						Pos++;
						continue;
					}
					if (Pos < Text.size() && Text[Pos] == '}') {
						Pos++;
						value = JsonValue(std::move(object));
						return true;
					}
					return Fail("',' or '}' is expected");
				}
			}
			if (c == '[') {
				Pos++;
				JsonValue::Array array;
				Space();
				if (Pos < Text.size() && Text[Pos] == ']') {
					Pos++;
					value = JsonValue(std::move(array));
					return true;
				}
				for (;;) {
					JsonValue element;
					if (!Value(element, depth + 1))
						return false;
					array.push_back(std::move(element));
					Space();
					if (Pos < Text.size() && Text[Pos] == ',') {
						Pos++;
						continue;
					}
					if (Pos < Text.size() && Text[Pos] == ']') {
						Pos++;
						value = JsonValue(std::move(array));
						return true;
					}
					return Fail("',' or ']' is expected");
				}
			}
			if (c == '"') {
				std::string text;
				if (!String(text))
					return false;
				value = JsonValue(std::move(text));
				return true;
			}
			if (Literal("true")) {
				value = JsonValue(true);
				return true;
			}
			if (Literal("false")) {
				value = JsonValue(false);
				return true;
			}
			if (Literal("null")) {
				value = JsonValue();
				return true;
			}
			if (c == '-' || (c >= '0' && c <= '9')) {
				auto start = Pos;
				if (Text[Pos] == '-')
					Pos++;
				while (Pos < Text.size() && (isdigit((unsigned char)Text[Pos]) || Text[Pos] == '.' || Text[Pos] == 'e' || Text[Pos] == 'E' ||
					Text[Pos] == '+' || Text[Pos] == '-'))
					Pos++;
				double number = 0;
				auto [end, ec] = std::from_chars(Text.data() + start, Text.data() + Pos, number);
				if (ec != std::errc() || end != Text.data() + Pos)
					return Fail("A bad number");
				value = JsonValue(number);
				return true;
			}
			return Fail("A value is expected");
		}
	};

	void Quote(std::string& out, std::string const& text) {
		out += '"';
		for (unsigned char c : text) {
			switch (c) {
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (c < 0x20)
						out += std::format("\\u{:04x}", c);
					else
						out += (char)c;
			}
		}
		out += '"';
	}
}

bool JsonValue::Parse(std::string_view text, JsonValue& value, std::string* error) {
	// (a byte order mark is allowed)
	if (text.starts_with("\xEF\xBB\xBF"))
		text.remove_prefix(3);
	Parser parser{ text };
	JsonValue result;
	bool ok = parser.Value(result, 0);
	if (ok) {
		parser.Space();
		if (parser.Pos != text.size())
			ok = parser.Fail("Text after the value");
	}
	if (!ok) {
		if (error)
			*error = parser.Error;
		return false;
	}
	value = std::move(result);
	return true;
}

JsonValue::Array const& JsonValue::AsArray() const {
	static Array const empty;
	return IsArray() ? *m_Array : empty;
}

JsonValue::Object const& JsonValue::AsObject() const {
	static Object const empty;
	return IsObject() ? *m_Object : empty;
}

JsonValue const& JsonValue::operator[](std::string const& name) const {
	static JsonValue const none;
	if (!IsObject())
		return none;
	auto it = m_Object->find(name);
	return it == m_Object->end() ? none : it->second;
}

std::string JsonValue::Write(bool pretty) const {
	std::string out;
	Write(out, pretty, 0);
	if (pretty)
		out += '\n';
	return out;
}

void JsonValue::Write(std::string& out, bool pretty, int depth) const {
	auto newline = [&](int indent) {
		if (pretty) {
			out += '\n';
			out.append(indent, '\t');
		}
	};
	switch (m_Kind) {
		case Kind::Null: out += "null"; break;
		case Kind::Bool: out += m_Bool ? "true" : "false"; break;
		case Kind::Number:
		{
			if (std::isfinite(m_Number) && m_Number == std::floor(m_Number) && std::fabs(m_Number) < 9007199254740992.0)
				out += std::format("{}", (int64_t)m_Number);
			else if (std::isfinite(m_Number))
				out += std::format("{}", m_Number);
			else
				out += "null";	// JSON has no infinity or NaN
			break;
		}
		case Kind::String: Quote(out, m_String); break;
		case Kind::Array:
			out += '[';
			for (size_t i = 0; i < m_Array->size(); i++) {
				if (i)
					out += ',';
				newline(depth + 1);
				(*m_Array)[i].Write(out, pretty, depth + 1);
			}
			if (!m_Array->empty())
				newline(depth);
			out += ']';
			break;
		case Kind::Object:
		{
			out += '{';
			bool first = true;
			for (auto const& [name, member] : *m_Object) {
				if (!first)
					out += ',';
				first = false;
				newline(depth + 1);
				Quote(out, name);
				out += pretty ? ": " : ":";
				member.Write(out, pretty, depth + 1);
			}
			if (!m_Object->empty())
				newline(depth);
			out += '}';
			break;
		}
	}
}
