#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

class CoffObject;
class LibArchive;

//
// Signatures of the functions of static libraries (the C runtime, the STL...), to name them in images that have no symbols.
// A signature is a function's code without the bytes the linker writes (where the object has relocations: addresses, the
// targets of calls): code of an image is that function if the other bytes are the same. What the function calls (its
// relocations of calls and jumps) names the functions of the image there as well.
//
class LibrarySignatures {
public:
	struct Signature {
		uint16_t Machine{};
		std::string Name;		// as the object has it (decorated)
		std::vector<uint8_t> Code;
		std::vector<std::pair<uint32_t, uint8_t>> Masked;		// the bytes the linker writes: offset, count (in order)
		std::vector<std::pair<uint32_t, std::string>> Calls;	// calls and jumps: the offset of the target's field, its name
		bool Ambiguous{ false };	// the same code is of functions of other names
	};

	// The functions of an object's code (each from its symbol to the next one, or to the end of its section); those too short
	// to tell apart are left out. The number added.
	size_t AddObject(CoffObject const& object);
	// Those of the objects of a static library (the import records of an import library are not code)
	size_t AddLibrary(LibArchive const& library);
	// (after adding: the same code of other names is ambiguous, the same function in several libraries is one)
	void Finish();

	bool Save(std::wstring const& path) const;
	// Adds those of a file of signatures (written by Save); false if it is not one
	bool Load(std::wstring const& path);
	size_t Size() const { return m_Signatures.size(); }
	std::vector<Signature> const& Signatures() const { return m_Signatures; }

	// The function whose code starts 'code' (as much as the image has there): the longest that matches; null if none does
	// (or one that is ambiguous)
	Signature const* Find(uint16_t machine, std::span<const uint8_t> code) const;
	// What a function of an image at 'address' that matches a signature calls: the targets (from its code) and their names
	static std::vector<std::pair<uint64_t, std::string>> CallsOf(Signature const& signature, std::span<const uint8_t> code, uint64_t address);

	// the least code (and of it, the bytes the linker does not write) of a function that has a signature
	static constexpr size_t MinLength = 16, MinFixed = 12;

private:
	void Index() const;

	std::vector<Signature> m_Signatures;
	// by machine, the number of fixed bytes they start with (at most 16), and those bytes: the signatures
	mutable std::unordered_map<uint64_t, std::vector<uint32_t>> m_Index;
	mutable std::unordered_map<uint16_t, std::vector<uint32_t>> m_Prefixes;
	mutable bool m_Indexed{ false };
};
