#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

//
// What the user adds to a file: names and types of its functions and variables (that the decompiler uses), bookmarks and
// comments. Places are RVAs. It is kept in a JSON file of its own, in the user's folder of the application, named by the
// SHA-256 of the file's content: the same file has them wherever it is, and a file that changes starts with none.
//
class Annotations {
public:
	// A variable as the user named or typed it: its name, and its type (the type it was shown with, unless the user changed
	// it: Retyped)
	struct Variable {
		std::string Name;
		std::string Type;
		bool Retyped{ false };
		bool operator==(Variable const&) const = default;
	};
	struct Bookmark {
		uint32_t Rva{};
		std::wstring Name;
		bool operator==(Bookmark const&) const = default;
	};

	// The file that keeps the annotations of a file with this SHA-256 (hex): %LOCALAPPDATA%\TotalPE\Annotations\<sha256>.json
	static std::wstring StorePath(std::string const& sha256);
	// Reads the annotations of a file (none if the store does not exist yet); false if the store cannot be read. 'file' (the
	// path of the annotated file) is kept in the store, for whoever reads it.
	bool Load(std::wstring const& store, std::wstring const& file);
	// Writes them where they were loaded from (a file that has none any more has no store)
	bool Save() const;
	bool IsOpen() const { return !m_Store.empty(); }
	std::wstring const& StoreFile() const { return m_Store; }
	bool Empty() const;

	// the JSON of the store, and back (false if it is not what a store has)
	std::string ToJson() const;
	bool FromJson(std::string const& json);

	std::map<uint32_t, std::string> Functions;		// names, by the function's RVA
	std::map<uint32_t, std::map<std::string, Variable>> Variables;	// by the function's RVA, by what the decompiler calls the variable
	std::map<uint32_t, Variable> Globals;			// by the RVA of the variable
	std::map<uint32_t, std::wstring> Comments;		// by the RVA of the instruction (or data)
	std::vector<Bookmark> Bookmarks;				// in the order they were added

private:
	std::wstring m_Store, m_File;
};
