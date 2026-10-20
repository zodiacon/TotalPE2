#include "pch.h"
#include "Output.h"

Output::~Output() {
	Flush();
	if (m_Owned && m_Handle)
		::CloseHandle(m_Handle);
}

Output& Output::StdOut() {
	static Output out;
	static bool attached = (out.Attach(STD_OUTPUT_HANDLE), true);
	return out;
}

Output& Output::StdErr() {
	static Output err;
	static bool attached = (err.Attach(STD_ERROR_HANDLE), true);
	return err;
}

void Output::Attach(DWORD stdHandle) {
	m_Handle = ::GetStdHandle(stdHandle);
	DWORD mode;
	m_Console = m_Handle && ::GetConsoleMode(m_Handle, &mode);
}

bool Output::Create(std::wstring const& path) {
	auto h = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	m_Handle = h;
	m_Owned = true;
	m_Console = false;
	return true;
}

void Output::Write(std::string_view text) {
	if (!m_Handle || text.empty())
		return;
	if (m_Console) {
		int n = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
		std::wstring wide(n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), wide.data(), n);
		DWORD written;
		::WriteConsoleW(m_Handle, wide.data(), (DWORD)wide.size(), &written, nullptr);
		return;
	}
	m_Buffer += text;
	if (m_Buffer.size() >= 1 << 16)
		Flush();
}

void Output::Flush() {
	if (m_Buffer.empty() || !m_Handle)
		return;
	DWORD written;
	::WriteFile(m_Handle, m_Buffer.data(), (DWORD)m_Buffer.size(), &written, nullptr);
	m_Buffer.clear();
}

std::string ToUtf8(std::wstring_view text) {
	if (text.empty())
		return {};
	int n = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
	std::string s(n, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), s.data(), n, nullptr, nullptr);
	return s;
}

std::string CsvRow(std::vector<std::string> const& values) {
	std::string row;
	for (size_t i = 0; i < values.size(); i++) {
		if (i)
			row += ',';
		auto const& v = values[i];
		if (v.find_first_of(",\"\r\n") == std::string::npos && (v.empty() || (v.front() != ' ' && v.back() != ' '))) {
			row += v;
			continue;
		}
		row += '"';
		for (char c : v) {
			if (c == '"')
				row += '"';
			row += c;
		}
		row += '"';
	}
	return row;
}

std::vector<std::string> CsvValues(JsonValue const& row, std::vector<std::string> const& columns) {
	std::vector<std::string> values;
	for (auto const& c : columns) {
		auto const& v = row[c];
		switch (v.GetKind()) {
			case JsonValue::Kind::Null: values.emplace_back(); break;
			case JsonValue::Kind::String: values.push_back(v.AsString()); break;
			default: values.push_back(v.Write(false)); break;
		}
	}
	return values;
}

namespace {
	std::string Thousands(double value) {
		auto digits = std::format("{}", (uint64_t)value);
		std::string result;
		for (size_t i = 0; i < digits.size(); i++) {
			if (i && (digits.size() - i) % 3 == 0)
				result += ',';
			result += digits[i];
		}
		return result;
	}

	std::string Join(JsonValue const& array, std::string_view separator = " ") {
		std::string result;
		for (auto const& item : array.AsArray()) {
			if (!result.empty())
				result += separator;
			result += item.AsString();
		}
		return result;
	}

	void Line(std::string& out, std::string_view label, std::string_view value) {
		if (!value.empty())
			out += std::format("  {:<11} {}\n", label, value);
	}
}

std::string FormatReportText(JsonValue const& report) {
	std::string out = report["file"]["path"].AsString() + "\n";
	if (report["error"].IsString()) {
		Line(out, "Error", report["error"].AsString());
		return out;
	}

	auto const& h = report["headers"];
	if (h.IsObject()) {
		auto kind = h["isDriver"].AsBool() ? "driver" : h["isDll"].AsBool() ? "DLL" : "executable";
		Line(out, "Type", std::format("{} {} {}, {}{}", h["format"].AsString(), h["machine"].AsString(), kind,
			h["subsystem"].AsString(), h["isManaged"].AsBool() ? ", .NET" : ""));
		if (h["reproducible"].AsBool())
			Line(out, "Timestamp", std::format("reproducible build (hash 0x{:08X})", (uint32_t)h["timestampRaw"].AsNumber()));
		else
			Line(out, "Timestamp", std::format("{} ({})", h["timestamp"].AsString("-"), Thousands(h["timestampRaw"].AsNumber())));
		Line(out, "Entry point", std::format("{} (image base {})", h["entryPoint"].AsString(), h["imageBase"].AsString()));
		if (!h["checksum"]["valid"].AsBool(true) && h["checksum"]["stored"].AsString() != "0x0")
			Line(out, "Checksum", std::format("{} (should be {})", h["checksum"]["stored"].AsString(), h["checksum"]["computed"].AsString()));
	}

	auto const& file = report["file"];
	if (file["size"].IsNumber())
		Line(out, "Size", Thousands(file["size"].AsNumber()) + " bytes");
	Line(out, "SHA-256", file["sha256"].AsString());
	Line(out, "Imphash", file["imphash"].AsString());

	auto const& version = report["version"];
	if (version.IsObject()) {
		auto const& strings = version["strings"];
		std::string text = version["fixed"]["fileVersion"].AsString(strings["FileVersion"].AsString());
		for (auto name : { "CompanyName", "FileDescription" })
			if (auto s = strings[name].AsString(); !s.empty())
				text += (text.empty() ? "" : "  ") + s;
		Line(out, "Version", text);
	}

	for (auto const& d : report["debug"].AsArray())
		if (d["pdb"].IsString()) {
			Line(out, "PDB", std::format("{} {} {}", d["pdb"].AsString(), d["guid"].AsString(), d["age"].Write(false)));
			break;
		}

	auto const& sig = report["signature"];
	if (sig.IsObject()) {
		std::string text = "none";
		if (sig["kind"].AsString() == "catalog") {
			auto const& catalog = sig["catalog"];
			text = "in the catalog " + catalog["path"].AsString();
			if (catalog["signature"]["signer"].IsObject())
				text += " (signed by " + catalog["signature"]["signer"]["subject"].AsString() + ")";
		}
		else if (sig["present"].AsBool()) {
			auto const& first = sig["signatures"].AsArray();
			text = "signed";
			if (!first.empty()) {
				if (first[0]["signer"].IsObject())
					text += " by " + first[0]["signer"]["subject"].AsString();
				text += first[0]["digestMatches"].AsBool() ? " (digest matches)" : " (DIGEST DOES NOT MATCH)";
			}
		}
		if (sig["trusted"].IsBool())
			text += sig["trusted"].AsBool() ? ", trusted" : ", NOT trusted: " + sig["trust"].AsString();
		Line(out, "Signature", text);
	}

	if (report["security"].IsArray()) {
		std::string text;
		for (auto const& f : report["security"].AsArray()) {
			auto state = f["state"].AsString();
			if (state == "notApplicable" || state == "info")
				continue;
			text += std::format("{}{}: {}", text.empty() ? "" : ", ", f["name"].AsString(), state);
		}
		Line(out, "Security", text);
	}

	if (report["imports"].IsArray()) {
		size_t modules = 0, functions = 0;
		for (auto const& m : report["imports"].AsArray()) {
			modules++;
			functions += m["functions"].AsArray().size();
		}
		Line(out, "Imports", std::format("{} functions from {} modules", functions, modules));
	}
	if (report["exports"].IsObject())
		Line(out, "Exports", std::format("{} ({})", Thousands(report["exports"]["count"].AsNumber()), report["exports"]["dllName"].AsString()));
	if (report["tls"].IsObject())
		Line(out, "TLS", std::format("{} callbacks", report["tls"]["callbacks"].AsArray().size()));
	auto const& overlay = report["overlay"];
	if (overlay["size"].AsNumber() > 0)
		Line(out, "Overlay", std::format("{} bytes at {}, {}entropy {}", Thousands(overlay["size"].AsNumber()), overlay["offset"].AsString(),
			overlay["kind"].IsString() ? overlay["kind"].AsString() + ", " : "", overlay["entropy"].Write(false)));
	if (report["rtti"].IsArray())
		Line(out, "RTTI", std::format("{} classes", report["rtti"].AsArray().size()));

	if (!report["sections"].AsArray().empty()) {
		out += "  Sections\n";
		for (auto const& s : report["sections"].AsArray())
			out += std::format("    {:<9} {:>10} {:>12} {:>5.2f}{} {}\n", s["name"].AsString(), s["rva"].AsString(),
				Thousands(s["virtualSize"].AsNumber()), s["entropy"].AsNumber(), s["highEntropy"].AsBool() ? "!" : " ", Join(s["flags"]));
	}
	if (!report["anomalies"].AsArray().empty()) {
		out += "  Anomalies\n";
		for (auto const& a : report["anomalies"].AsArray())
			out += std::format("    [{}] {}: {}{}\n", a["severity"].AsString(), a["category"].AsString(), a["message"].AsString(),
				a["offset"].IsString() ? " (at " + a["offset"].AsString() + ")" : "");
	}
	if (!report["errors"].AsArray().empty()) {
		out += "  Errors\n";
		for (auto const& e : report["errors"].AsArray())
			out += "    " + e.AsString() + "\n";
	}
	return out;
}

std::string FormatDiffText(JsonValue const& diff) {
	auto status = diff["status"].AsString();
	if (status == "added")
		return "only in new: " + diff["right"]["path"].AsString() + "\n";
	if (status == "removed")
		return "only in old: " + diff["left"]["path"].AsString() + "\n";

	std::string out = std::format("--- {}\n+++ {}\n", diff["left"]["path"].AsString(), diff["right"]["path"].AsString());
	if (status == "error")
		return out + "  error: " + diff["error"].AsString() + "\n";
	if (status == "identical")
		return out + "  the files are identical\n";

	for (auto const& item : diff["items"].AsArray()) {
		auto const& left = item["left"], &right = item["right"];
		std::string values;
		if (left.IsString() && right.IsString())
			values = left.AsString() + " -> " + right.AsString();
		else if (left.IsString() || right.IsString())
			values = left.IsString() ? left.AsString() : right.AsString();
		auto details = item["details"].AsString();
		out += std::format("  {:<8} {:<9} {}{}{}\n", item["status"].AsString(), item["category"].AsString(), item["name"].AsString(),
			values.empty() ? "" : ": " + values, details.empty() || details == values ? "" : "  (" + details + ")");
	}
	auto const& counts = diff["counts"];
	out += std::format("  {} changed, {} added, {} removed\n", counts["changed"].Write(false), counts["added"].Write(false), counts["removed"].Write(false));
	return out;
}
