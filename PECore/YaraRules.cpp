#include "pch.h"
#include "YaraRules.h"
#include <filesystem>
#include <unordered_set>

// libyara defines some of the same error names as winerror.h, with the same values
#pragma warning(push)
#pragma warning(disable: 4005)
#include <yara.h>
#pragma warning(pop)

// what libyara (and OpenSSL, for the hash module) need of Windows
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

namespace fs = std::filesystem;

namespace {
	std::string Utf8(std::wstring_view w) {
		if (w.empty())
			return {};
		int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(n, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
		return s;
	}

	std::wstring Wide(std::string_view s) {
		if (s.empty())
			return {};
		int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
		return w;
	}

	// libyara is initialized once for the process (it is not finalized: the process ends with it)
	bool Initialize() {
		static const bool ok = yr_initialize() == ERROR_SUCCESS;
		return ok;
	}

	// the external variables that community rules use, set for each file that is scanned
	constexpr const char* Externals[] = { "filename", "filepath", "extension", "filetype", "owner" };

	bool IsRuleFile(fs::path const& path) {
		auto ext = path.extension().wstring();
		return _wcsicmp(ext.c_str(), L".yar") == 0 || _wcsicmp(ext.c_str(), L".yara") == 0;
	}

	bool IsCompiled(fs::path const& path) {
		return _wcsicmp(path.extension().wstring().c_str(), L".yarc") == 0;
	}

	struct CompilerContext {
		std::vector<YaraDiagnostic>* Diagnostics;
		std::wstring File;
		size_t Errors{};
	};

	void CompilerCallback(int level, const char* file, int line, const YR_RULE*, const char* message, void* user) {
		auto ctx = static_cast<CompilerContext*>(user);
		YaraDiagnostic d;
		d.Error = level == YARA_ERROR_LEVEL_ERROR;
		d.File = file && *file ? Wide(file) : ctx->File;
		d.Line = line;
		d.Message = message ? message : "";
		ctx->Diagnostics->push_back(std::move(d));
		if (level == YARA_ERROR_LEVEL_ERROR)
			ctx->Errors++;
	}

	struct RuleSource {
		std::wstring File;		// empty for text
		std::string Text;
		std::string Namespace;
	};

	struct ScanState {
		std::vector<YaraMatch>* Matches;
		std::unordered_set<YR_STRING const*> TooMany;
	};

	int ScanCallback(YR_SCAN_CONTEXT* context, int message, void* data, void* user) {
		auto state = static_cast<ScanState*>(user);
		if (message == CALLBACK_MSG_TOO_MANY_MATCHES) {
			state->TooMany.insert(static_cast<YR_STRING const*>(data));
			return CALLBACK_CONTINUE;
		}
		if (message != CALLBACK_MSG_RULE_MATCHING)
			return CALLBACK_CONTINUE;

		auto rule = static_cast<YR_RULE*>(data);
		YaraMatch m;
		m.Rule = rule->identifier;
		m.Namespace = rule->ns && rule->ns->name ? rule->ns->name : "";
		const char* tag;
		yr_rule_tags_foreach(rule, tag)
			m.Tags.emplace_back(tag);
		YR_META* meta;
		yr_rule_metas_foreach(rule, meta) {
			std::string value;
			switch (meta->type) {
				case META_TYPE_INTEGER: value = std::to_string(meta->integer); break;
				case META_TYPE_BOOLEAN: value = meta->integer ? "true" : "false"; break;
				default: value = meta->string ? meta->string : ""; break;
			}
			m.Meta.emplace_back(meta->identifier ? meta->identifier : "", std::move(value));
		}
		YR_STRING* string;
		yr_rule_strings_foreach(rule, string) {
			if (state->TooMany.contains(string))
				m.TooManyMatches = true;
			YR_MATCH* match;
			yr_string_matches_foreach(context, string, match) {
				m.StringMatchCount++;
				if (m.Strings.size() < YaraMatch::MaxStringMatches) {
					YaraStringMatch s;
					s.Identifier = string->identifier ? string->identifier : "";
					s.Offset = (uint64_t)(match->base + match->offset);
					s.Length = (uint32_t)match->match_length;
					auto n = std::min<size_t>(match->data_length, YaraStringMatch::MaxDataBytes);
					if (match->data && n)
						s.Data.assign(match->data, match->data + n);
					m.Strings.push_back(std::move(s));
				}
			}
		}
		state->Matches->push_back(std::move(m));
		return CALLBACK_CONTINUE;
	}
}

struct YaraRules::Impl {
	YR_RULES* Rules{ nullptr };
	~Impl() {
		if (Rules)
			yr_rules_destroy(Rules);
	}

	// Compiles the sources together; those with errors are left out and the rest compiled again (the compiler of libyara
	// cannot be used after an error)
	static YR_RULES* Compile(std::vector<RuleSource> sources, std::vector<YaraDiagnostic>& diagnostics) {
		std::vector<bool> bad(sources.size());
		for (;;) {
			YR_COMPILER* compiler = nullptr;
			if (yr_compiler_create(&compiler) != ERROR_SUCCESS)
				return nullptr;
			auto destroy = wil::scope_exit([&] { yr_compiler_destroy(compiler); });
			for (auto name : Externals)
				yr_compiler_define_string_variable(compiler, name, "");

			// the warnings and errors of this round: those of the sources that are kept are the ones to tell
			std::vector<YaraDiagnostic> round;
			CompilerContext ctx{ &round };
			yr_compiler_set_callback(compiler, CompilerCallback, &ctx);
			int failed = -1;
			for (size_t i = 0; i < sources.size(); i++) {
				if (bad[i])
					continue;
				auto const& s = sources[i];
				ctx.File = s.File;
				int errors;
				if (s.File.empty())
					errors = yr_compiler_add_string(compiler, s.Text.c_str(), s.Namespace.c_str());
				else {
					FILE* f = nullptr;
					if (_wfopen_s(&f, s.File.c_str(), L"rb") != 0 || !f) {
						diagnostics.push_back({ true, s.File, 0, "the file cannot be opened" });
						bad[i] = true;
						continue;
					}
					errors = yr_compiler_add_file(compiler, f, s.Namespace.c_str(), Utf8(s.File).c_str());
					fclose(f);
				}
				if (errors) {
					failed = (int)i;
					break;
				}
			}
			if (failed >= 0) {
				// keep the errors of the source that failed, and compile the others again without it
				for (auto& d : round)
					if (d.Error)
						diagnostics.push_back(std::move(d));
				bad[failed] = true;
				continue;
			}
			diagnostics.insert(diagnostics.end(), round.begin(), round.end());
			if (std::all_of(bad.begin(), bad.end(), [](bool b) { return b; }))
				return nullptr;
			YR_RULES* rules = nullptr;
			if (yr_compiler_get_rules(compiler, &rules) != ERROR_SUCCESS)
				return nullptr;
			return rules;
		}
	}
};

YaraRules::YaraRules() : m_Impl(std::make_unique<Impl>()) {
	Initialize();
}

YaraRules::~YaraRules() = default;
YaraRules::YaraRules(YaraRules&&) noexcept = default;
YaraRules& YaraRules::operator=(YaraRules&&) noexcept = default;

bool YaraRules::IsLoaded() const {
	return m_Impl && m_Impl->Rules;
}

size_t YaraRules::RuleCount() const {
	if (!IsLoaded())
		return 0;
	size_t count = 0;
	YR_RULE* rule;
	yr_rules_foreach(m_Impl->Rules, rule)
		count++;
	return count;
}

bool YaraRules::Load(std::wstring const& path) {
	m_Diagnostics.clear();
	m_Impl = std::make_unique<Impl>();
	m_Source = path;
	if (!Initialize()) {
		m_Diagnostics.push_back({ true, path, 0, "libyara could not be initialized" });
		return false;
	}

	std::error_code ec;
	fs::path p(path);
	if (fs::is_regular_file(p, ec) && IsCompiled(p)) {
		YR_RULES* rules = nullptr;
		auto code = yr_rules_load(Utf8(path).c_str(), &rules);
		if (code != ERROR_SUCCESS) {
			m_Diagnostics.push_back({ true, path, 0, ErrorMessage(code) });
			return false;
		}
		m_Impl->Rules = rules;
		return true;
	}

	std::vector<RuleSource> sources;
	if (fs::is_directory(p, ec)) {
		std::vector<fs::path> files;
		for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
			if (it->is_regular_file(ec) && IsRuleFile(it->path()))
				files.push_back(it->path());
		std::sort(files.begin(), files.end());
		for (auto const& f : files)
			sources.push_back({ f.wstring(), {}, Utf8(f.stem().wstring()) });
		if (sources.empty()) {
			m_Diagnostics.push_back({ true, path, 0, "the directory has no rule files (.yar, .yara)" });
			return false;
		}
	}
	else if (fs::is_regular_file(p, ec))
		sources.push_back({ path, {}, "default" });
	else {
		m_Diagnostics.push_back({ true, path, 0, "not found" });
		return false;
	}

	m_Impl->Rules = Impl::Compile(std::move(sources), m_Diagnostics);
	return IsLoaded();
}

bool YaraRules::LoadText(std::string const& text, std::string const& ns) {
	m_Diagnostics.clear();
	m_Impl = std::make_unique<Impl>();
	m_Source.clear();
	if (!Initialize())
		return false;
	m_Impl->Rules = Impl::Compile({ RuleSource{ {}, text, ns } }, m_Diagnostics);
	return IsLoaded();
}

bool YaraRules::Scan(std::span<const std::byte> data, std::wstring const& path, std::vector<YaraMatch>& matches, std::string* error,
	int timeoutSeconds) const {
	matches.clear();
	if (!IsLoaded()) {
		if (error)
			*error = "no rules are loaded";
		return false;
	}
	YR_SCANNER* scanner = nullptr;
	if (auto code = yr_scanner_create(m_Impl->Rules, &scanner); code != ERROR_SUCCESS) {
		if (error)
			*error = ErrorMessage(code);
		return false;
	}
	auto destroy = wil::scope_exit([&] { yr_scanner_destroy(scanner); });

	// the externals of the file (compiled rules may not have them all: then they are not set)
	fs::path p(path);
	auto filename = Utf8(p.filename().wstring()), filepath = Utf8(path), extension = Utf8(p.extension().wstring());
	if (!extension.empty() && extension[0] == '.')
		extension.erase(0, 1);
	yr_scanner_define_string_variable(scanner, "filename", filename.c_str());
	yr_scanner_define_string_variable(scanner, "filepath", filepath.c_str());
	yr_scanner_define_string_variable(scanner, "extension", extension.c_str());
	yr_scanner_define_string_variable(scanner, "filetype", "pe");

	ScanState state{ &matches };
	yr_scanner_set_callback(scanner, ScanCallback, &state);
	yr_scanner_set_flags(scanner, SCAN_FLAGS_REPORT_RULES_MATCHING);
	if (timeoutSeconds > 0)
		yr_scanner_set_timeout(scanner, timeoutSeconds);
	auto code = yr_scanner_scan_mem(scanner, (const uint8_t*)data.data(), data.size());
	if (code != ERROR_SUCCESS) {
		if (error)
			*error = ErrorMessage(code);
		return false;
	}
	return true;
}

std::string YaraRules::ErrorMessage(int code) {
	switch (code) {
		case ERROR_SUCCESS: return "success";
		case ERROR_INSUFFICIENT_MEMORY: return "not enough memory";
		case ERROR_COULD_NOT_OPEN_FILE: return "the file cannot be opened";
		case ERROR_COULD_NOT_MAP_FILE: return "the file cannot be mapped";
		case ERROR_INVALID_FILE: return "not a file of compiled rules";
		case ERROR_CORRUPT_FILE: return "the file of compiled rules is damaged";
		case ERROR_UNSUPPORTED_FILE_VERSION: return "the compiled rules are of another version of YARA";
		case ERROR_EXEC_STACK_OVERFLOW: return "a rule is too complex (stack overflow)";
		case ERROR_SCAN_TIMEOUT: return "the scan timed out";
		case ERROR_TOO_MANY_SCAN_THREADS: return "too many threads scan at once";
		case ERROR_CALLBACK_ERROR: return "the scan was stopped";
		case ERROR_INVALID_ARGUMENT: return "invalid argument";
		case ERROR_TOO_MANY_MATCHES: return "too many matches";
		case ERROR_TOO_MANY_RE_FIBERS: return "a regular expression is too complex";
		case ERROR_INTERNAL_FATAL_ERROR: return "internal error of libyara";
	}
	return std::format("libyara error {}", code);
}
