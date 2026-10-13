#include "pch.h"
#include "Annotations.h"
#include "Json.h"
#include <fstream>
#include <sstream>

namespace {
	std::string Utf8(std::wstring const& text) {
		if (text.empty())
			return {};
		int len = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
		std::string result(len, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), len, nullptr, nullptr);
		return result;
	}

	std::wstring Wide(std::string const& text) {
		if (text.empty())
			return {};
		int len = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
		std::wstring result(len, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), len);
		return result;
	}

	// RVAs are written in hex ("0x1A2B"), as the rest of the application shows them
	std::string RvaText(uint32_t rva) {
		return std::format("0x{:X}", rva);
	}

	bool ParseRva(std::string const& text, uint32_t& rva) {
		auto digits = text.starts_with("0x") || text.starts_with("0X") ? text.substr(2) : text;
		if (digits.empty() || digits.size() > 8)
			return false;
		auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), rva, 16);
		return ec == std::errc() && end == digits.data() + digits.size();
	}

	JsonValue VariableJson(Annotations::Variable const& v) {
		JsonValue::Object object{ { "name", v.Name }, { "type", v.Type } };
		if (v.Retyped)
			object["retyped"] = true;
		return object;
	}

	Annotations::Variable VariableOf(JsonValue const& value) {
		return { value["name"].AsString(), value["type"].AsString(), value["retyped"].AsBool() };
	}
}

std::wstring Annotations::StorePath(std::string const& sha256) {
	WCHAR folder[MAX_PATH];
	if (!::ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\TotalPE\\Annotations", folder, _countof(folder)))
		return {};
	return std::wstring(folder) + L"\\" + Wide(sha256) + L".json";
}

bool Annotations::Empty() const {
	return Functions.empty() && Variables.empty() && Globals.empty() && Comments.empty() && Bookmarks.empty();
}

bool Annotations::Load(std::wstring const& store, std::wstring const& file) {
	*this = {};
	m_Store = store;
	m_File = file;
	std::ifstream in(store, std::ios::binary);
	if (!in)
		return ::GetFileAttributesW(store.c_str()) == INVALID_FILE_ATTRIBUTES;	// none yet
	std::stringstream text;
	text << in.rdbuf();
	return FromJson(text.str());
}

bool Annotations::Save() const {
	if (m_Store.empty())
		return false;
	if (Empty()) {
		::DeleteFileW(m_Store.c_str());
		return true;
	}
	// the folder, and the folders it is in
	auto folder = m_Store.substr(0, m_Store.find_last_of(L'\\'));
	for (size_t pos = folder.find(L'\\', 3); ; pos = folder.find(L'\\', pos + 1)) {
		::CreateDirectoryW(folder.substr(0, pos).c_str(), nullptr);
		if (pos == std::wstring::npos)
			break;
	}
	// written whole, then put in the place of the old one: a failure leaves the old one as it was
	auto temp = m_Store + L".new";
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if (!out)
			return false;
		out << ToJson();
		if (!out)
			return false;
	}
	return ::MoveFileExW(temp.c_str(), m_Store.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

std::string Annotations::ToJson() const {
	JsonValue::Object root{ { "file", Utf8(m_File) }, { "version", 1 } };
	if (!Functions.empty()) {
		JsonValue::Object functions;
		for (auto const& [rva, name] : Functions)
			functions[RvaText(rva)] = name;
		root["functions"] = std::move(functions);
	}
	if (!Variables.empty()) {
		JsonValue::Object variables;
		for (auto const& [rva, list] : Variables) {
			JsonValue::Object ofFunction;
			for (auto const& [key, v] : list)
				ofFunction[key] = VariableJson(v);
			variables[RvaText(rva)] = std::move(ofFunction);
		}
		root["variables"] = std::move(variables);
	}
	if (!Globals.empty()) {
		JsonValue::Object globals;
		for (auto const& [rva, v] : Globals)
			globals[RvaText(rva)] = VariableJson(v);
		root["globals"] = std::move(globals);
	}
	if (!Comments.empty()) {
		JsonValue::Object comments;
		for (auto const& [rva, text] : Comments)
			comments[RvaText(rva)] = Utf8(text);
		root["comments"] = std::move(comments);
	}
	if (!Bookmarks.empty()) {
		JsonValue::Array bookmarks;
		for (auto const& b : Bookmarks)
			bookmarks.push_back(JsonValue::Object{ { "rva", RvaText(b.Rva) }, { "name", Utf8(b.Name) } });
		root["bookmarks"] = std::move(bookmarks);
	}
	return JsonValue(std::move(root)).Write();
}

bool Annotations::FromJson(std::string const& json) {
	JsonValue root;
	if (!JsonValue::Parse(json, root) || !root.IsObject())
		return false;
	Functions.clear();
	Variables.clear();
	Globals.clear();
	Comments.clear();
	Bookmarks.clear();
	uint32_t rva;
	for (auto const& [key, name] : root["functions"].AsObject())
		if (ParseRva(key, rva) && name.IsString() && !name.AsString().empty())
			Functions[rva] = name.AsString();
	for (auto const& [key, list] : root["variables"].AsObject()) {
		if (!ParseRva(key, rva))
			continue;
		for (auto const& [name, v] : list.AsObject())
			if (auto variable = VariableOf(v); !variable.Name.empty())
				Variables[rva][name] = variable;
	}
	for (auto const& [key, v] : root["globals"].AsObject())
		if (auto variable = VariableOf(v); ParseRva(key, rva) && !variable.Name.empty())
			Globals[rva] = variable;
	for (auto const& [key, text] : root["comments"].AsObject())
		if (ParseRva(key, rva) && text.IsString() && !text.AsString().empty())
			Comments[rva] = Wide(text.AsString());
	for (auto const& b : root["bookmarks"].AsArray())
		if (ParseRva(b["rva"].AsString(), rva))
			Bookmarks.push_back({ rva, Wide(b["name"].AsString()) });
	return true;
}
