#pragma once

#include <filesystem>
#include <string>
#include <vector>

// The PE file extensions that a directory is searched for by default
inline constexpr wchar_t DefaultInclude[] = L"*.exe;*.dll;*.sys;*.ocx;*.cpl;*.scr;*.drv;*.efi;*.ax;*.acm;*.tsp;*.winmd;*.mui";

struct InputOptions {
	bool Recursive{ false };
	std::wstring Include{ DefaultInclude };	// patterns separated by ';' for the files of directories
};

// The files that the arguments name: a file as it is, the files of a directory that match the include patterns, and the
// files that match a wildcard ("C:\Drivers\*.sys"). Recursive searches the subdirectories too (without following links).
// Sorted, without duplicates. What cannot be found or read goes into 'problems'.
std::vector<std::wstring> ExpandInputs(std::vector<std::wstring> const& args, InputOptions const& options, std::vector<std::wstring>& problems);

// The files of a directory that match the include patterns (and of its subdirectories, if recursive), as paths relative to
// it, sorted without regard to case
std::vector<std::wstring> ListDirectory(std::filesystem::path const& dir, InputOptions const& options, std::vector<std::wstring>& problems);
