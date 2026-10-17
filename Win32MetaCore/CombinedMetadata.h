#pragma once

#include <ClrMetadata.h>
#include <PEFile.h>
#include <memory>
#include <unordered_map>

//
// Several metadata files (Windows.Win32.winmd, Windows.Wdk.winmd) as one: what ClrMetadata has of each, the rows of each
// table one after the other (the first file's are its own: its tokens are as they were). A file's references to the types
// of another (Windows.Wdk's to Windows.Win32's) are those types: in the signatures, the interfaces of a type, they are
// TypeDef tokens. What Win32Api uses of ClrMetadata, by the same names.
//
class CombinedMetadata {
public:
	// a file added (false if it cannot be read, or has no metadata)
	bool Add(std::wstring const& path);
	size_t ModuleCount() const {
		return m_Modules.size();
	}
	// the module a TypeDef, MethodDef or Field row is of (0: the first)
	size_t ModuleOf(uint32_t token) const;

	std::vector<ClrTypeDef> const& Types() const {
		return m_Types;
	}
	std::vector<ClrMethodDef> const& Methods() const {
		return m_Methods;
	}
	std::vector<ClrFieldDef> const& Fields() const {
		return m_Fields;
	}
	std::vector<uint32_t> MethodsOf(uint32_t type) const;
	std::vector<uint32_t> FieldsOf(uint32_t type) const;
	std::wstring TypeName(uint32_t token) const;
	std::wstring TypeKind(uint32_t type) const;
	// a TypeDef row of these files (this, row); none for what is of another assembly
	std::pair<CombinedMetadata const*, uint32_t> ResolveType(uint32_t token) const;
	std::vector<uint32_t> Interfaces(uint32_t type) const;
	std::vector<ClrCustomAttribute> CustomAttributes(uint32_t token) const;
	std::vector<uint32_t> CustomAttributeTypes(uint32_t token) const;
	std::wstring AttributeCSharp(ClrCustomAttribute const& attribute) const;
	bool MethodSigTypes(uint32_t method, ClrSigType& result, std::vector<ClrSigType>& params) const;
	bool FieldSigType(uint32_t field, ClrSigType& type) const;
	std::wstring ParamName(uint32_t method, uint32_t sequence) const;
	uint16_t ParamFlags(uint32_t method, uint32_t sequence) const;
	uint32_t ParamToken(uint32_t method, uint32_t sequence) const;
	std::optional<ClrPInvoke> PInvoke(uint32_t method) const;
	std::shared_ptr<ClrEnumInfo const> EnumInfo(uint32_t token) const;
	std::optional<int64_t> ConstantInteger(uint32_t token) const;
	std::wstring ConstantCSharp(uint32_t token) const;
	std::pair<uint16_t, uint32_t> ClassLayout(uint32_t type) const;
	std::optional<uint32_t> FieldOffset(uint32_t field) const;

private:
	static constexpr int TableCount = 64;
	struct Module {
		PEFile Pe;
		ClrMetadata Md;
		uint32_t Base[TableCount]{};	// what its rows are after (the rows of the files before it)
	};

	// a token of these files: its module, and its token there (null if it is of none)
	std::pair<Module const*, uint32_t> Local(uint32_t token) const;
	uint32_t Global(Module const& module, uint32_t token) const;
	uint32_t Row(uint32_t token) const;
	// a type a module's token is: of these files (a TypeDef token), else the module's token, made global
	uint32_t TypeToken(Module const& module, uint32_t token) const;
	void Rebase(Module const& module, ClrSigType& type) const;

	std::vector<std::unique_ptr<Module>> m_Modules;
	uint32_t m_Rows[TableCount]{};		// of all of them
	std::vector<ClrTypeDef> m_Types;
	std::vector<ClrMethodDef> m_Methods;
	std::vector<ClrFieldDef> m_Fields;
	std::unordered_map<std::wstring, uint32_t> m_TypesByName;	// "Ns.Name": the TypeDef row (those not nested)
};
