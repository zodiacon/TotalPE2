#include "pch.h"
#include "Inputs.h"

namespace fs = std::filesystem;

namespace {
	bool Matches(std::wstring const& name, std::wstring const& patterns) {
		return ::PathMatchSpecExW(name.c_str(), patterns.c_str(), PMSF_MULTIPLE) == S_OK;
	}

	void AddDirectory(fs::path const& dir, std::wstring const& patterns, bool recursive, std::vector<std::wstring>& files,
		std::vector<std::wstring>& problems) {
		std::error_code ec;
		auto add = [&](fs::directory_entry const& entry) {
			std::error_code ec2;
			if (entry.is_regular_file(ec2) && Matches(entry.path().filename().wstring(), patterns))
				files.push_back(entry.path().wstring());
		};
		if (recursive) {
			fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
			for (; !ec && it != end; it.increment(ec))
				add(*it);
		}
		else {
			fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
			for (; !ec && it != end; it.increment(ec))
				add(*it);
		}
		if (ec)
			problems.push_back(std::format(L"{}: {}", dir.wstring(), std::wstring(ec.message().begin(), ec.message().end())));
	}
}

std::vector<std::wstring> ExpandInputs(std::vector<std::wstring> const& args, InputOptions const& options, std::vector<std::wstring>& problems) {
	std::vector<std::wstring> files;
	for (auto const& arg : args) {
		std::error_code ec;
		fs::path path(arg);
		if (arg.find_first_of(L"*?") != std::wstring::npos) {
			// a wildcard is of file names only: "dir\*.dll"
			auto dir = path.has_parent_path() ? path.parent_path() : fs::path(L".");
			auto before = files.size();
			AddDirectory(dir, path.filename().wstring(), options.Recursive, files, problems);
			if (files.size() == before)
				problems.push_back(std::format(L"{}: no files match", arg));
		}
		else if (fs::is_directory(path, ec))
			AddDirectory(path, options.Include, options.Recursive, files, problems);
		else if (fs::exists(path, ec))
			files.push_back(fs::absolute(path, ec).wstring());
		else
			problems.push_back(std::format(L"{}: not found", arg));
	}
	for (auto& f : files) {
		std::error_code ec;
		auto absolute = fs::absolute(f, ec);
		if (!ec)
			f = absolute.lexically_normal().wstring();
	}
	std::sort(files.begin(), files.end(), [](auto const& a, auto const& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
	files.erase(std::unique(files.begin(), files.end(), [](auto const& a, auto const& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }), files.end());
	return files;
}

std::vector<std::wstring> ListDirectory(fs::path const& dir, InputOptions const& options, std::vector<std::wstring>& problems) {
	std::vector<std::wstring> files;
	AddDirectory(dir, options.Include, options.Recursive, files, problems);
	for (auto& f : files)
		f = fs::path(f).lexically_relative(dir).wstring();
	std::sort(files.begin(), files.end(), [](auto const& a, auto const& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
	return files;
}
