#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

//
// A JSON value (RFC 8259): read from text and written as text (UTF-8). Numbers are doubles, as JavaScript has them:
// integers up to 2^53 are exact.
//
class JsonValue {
public:
	enum class Kind { Null, Bool, Number, String, Array, Object };
	using Array = std::vector<JsonValue>;
	using Object = std::map<std::string, JsonValue>;

	JsonValue() = default;
	JsonValue(std::nullptr_t) {}
	JsonValue(bool value) : m_Kind(Kind::Bool), m_Bool(value) {}
	JsonValue(double value) : m_Kind(Kind::Number), m_Number(value) {}
	JsonValue(int value) : JsonValue((double)value) {}
	JsonValue(int64_t value) : JsonValue((double)value) {}
	JsonValue(uint32_t value) : JsonValue((double)value) {}
	JsonValue(std::string value) : m_Kind(Kind::String), m_String(std::move(value)) {}
	JsonValue(char const* value) : JsonValue(std::string(value)) {}
	JsonValue(Array value) : m_Kind(Kind::Array), m_Array(std::make_shared<Array>(std::move(value))) {}
	JsonValue(Object value) : m_Kind(Kind::Object), m_Object(std::make_shared<Object>(std::move(value))) {}

	// The text as a value; false (with where and why) if it is not JSON
	static bool Parse(std::string_view text, JsonValue& value, std::string* error = nullptr);
	// as text: indented by tabs (pretty), or on one line
	std::string Write(bool pretty = true) const;

	Kind GetKind() const { return m_Kind; }
	bool IsNull() const { return m_Kind == Kind::Null; }
	bool IsBool() const { return m_Kind == Kind::Bool; }
	bool IsNumber() const { return m_Kind == Kind::Number; }
	bool IsString() const { return m_Kind == Kind::String; }
	bool IsArray() const { return m_Kind == Kind::Array; }
	bool IsObject() const { return m_Kind == Kind::Object; }

	// the value, or the fallback if it is of another kind
	bool AsBool(bool fallback = false) const { return IsBool() ? m_Bool : fallback; }
	double AsNumber(double fallback = 0) const { return IsNumber() ? m_Number : fallback; }
	std::string AsString(std::string const& fallback = {}) const { return IsString() ? m_String : fallback; }
	// empty if it is not one
	Array const& AsArray() const;
	Object const& AsObject() const;
	// a member of an object (null if there is none, or this is not an object)
	JsonValue const& operator[](std::string const& name) const;

private:
	void Write(std::string& out, bool pretty, int depth) const;

	Kind m_Kind{ Kind::Null };
	bool m_Bool{ false };
	double m_Number{ 0 };
	std::string m_String;
	std::shared_ptr<Array> m_Array;		// (shared: values are copied cheaply, and are not changed once made)
	std::shared_ptr<Object> m_Object;
};
