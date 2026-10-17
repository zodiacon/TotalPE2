#include "pch.h"
#include "CombinedMetadata.h"

namespace {
	uint32_t TableOf(uint32_t token) {
		return token >> 24;
	}
	uint32_t RowOf(uint32_t token) {
		return token & 0xFFFFFF;
	}
	uint32_t Token(uint32_t table, uint32_t row) {
		return (table << 24) | row;
	}
}

bool CombinedMetadata::Add(std::wstring const& path) {
	auto module = std::make_unique<Module>();
	if (!module->Pe.Open(path) || !module->Md.Parse(module->Pe) || !module->Md.HasMetadata())
		return false;
	auto& md = module->Md;
	// its rows after those there are
	for (int t = 0; t < TableCount; t++)
		module->Base[t] = m_Rows[t];
	for (auto const& table : md.Tables())
		if (table.Id >= 0 && table.Id < TableCount)
			m_Rows[table.Id] += table.Rows;
	auto const& base = module->Base;
	auto rebase = [&](uint32_t row, uint32_t table) { return row ? row + base[table] : 0; };
	for (auto type : md.Types()) {
		type.FirstField = rebase(type.FirstField, ClrTableId::Field);
		type.EndField = rebase(type.EndField, ClrTableId::Field);
		type.FirstMethod = rebase(type.FirstMethod, ClrTableId::MethodDef);
		type.EndMethod = rebase(type.EndMethod, ClrTableId::MethodDef);
		type.Enclosing = rebase(type.Enclosing, ClrTableId::TypeDef);
		if (type.Extends)
			type.Extends = Token(TableOf(type.Extends), RowOf(type.Extends) + base[TableOf(type.Extends)]);
		m_Types.push_back(std::move(type));
	}
	for (auto method : md.Methods()) {
		method.FirstParam = rebase(method.FirstParam, ClrTableId::Param);
		method.EndParam = rebase(method.EndParam, ClrTableId::Param);
		method.Type = rebase(method.Type, ClrTableId::TypeDef);
		m_Methods.push_back(std::move(method));
	}
	for (auto field : md.Fields()) {
		field.Type = rebase(field.Type, ClrTableId::TypeDef);
		m_Fields.push_back(std::move(field));
	}
	// (the rows the tables say, and what the lists have, are as many)
	m_Rows[ClrTableId::TypeDef] = (uint32_t)m_Types.size();
	m_Rows[ClrTableId::MethodDef] = (uint32_t)m_Methods.size();
	m_Rows[ClrTableId::Field] = (uint32_t)m_Fields.size();
	for (uint32_t row = 1; row <= md.Types().size(); row++) {
		auto const& t = md.Types()[row - 1];
		if (!t.Enclosing)
			m_TypesByName.try_emplace(t.Namespace + L"." + t.Name, row + base[ClrTableId::TypeDef]);
	}
	m_Modules.push_back(std::move(module));
	return true;
}

std::pair<CombinedMetadata::Module const*, uint32_t> CombinedMetadata::Local(uint32_t token) const {
	auto table = TableOf(token);
	auto row = RowOf(token);
	if (table >= TableCount || row == 0)
		return { m_Modules.empty() ? nullptr : m_Modules[0].get(), token };
	// the last of those whose rows it is after
	for (size_t i = m_Modules.size(); i-- > 0; ) {
		auto const& module = *m_Modules[i];
		if (row > module.Base[table])
			return { &module, Token(table, row - module.Base[table]) };
	}
	return { nullptr, 0 };
}

uint32_t CombinedMetadata::Global(Module const& module, uint32_t token) const {
	auto table = TableOf(token);
	if (table >= TableCount || RowOf(token) == 0)
		return token;
	return Token(table, RowOf(token) + module.Base[table]);
}

uint32_t CombinedMetadata::Row(uint32_t token) const {
	return RowOf(token);
}

size_t CombinedMetadata::ModuleOf(uint32_t token) const {
	auto [module, local] = Local(token);
	for (size_t i = 0; i < m_Modules.size(); i++)
		if (m_Modules[i].get() == module)
			return i;
	return 0;
}

uint32_t CombinedMetadata::TypeToken(Module const& module, uint32_t token) const {
	if (TableOf(token) == ClrTableId::TypeRef) {
		// a type of another file: by its name ("[Windows.Win32]Windows.Win32.Foundation.NTSTATUS")
		auto name = module.Md.TypeName(token);
		if (name.starts_with(L'['))
			name = name.substr(name.find(L']') + 1);
		if (auto it = m_TypesByName.find(name); it != m_TypesByName.end())
			return Token(ClrTableId::TypeDef, it->second);
	}
	return Global(module, token);
}

void CombinedMetadata::Rebase(Module const& module, ClrSigType& type) const {
	if (type.Token)
		type.Token = TypeToken(module, type.Token);
	for (auto& inner : type.Inner)
		Rebase(module, inner);
}

std::vector<uint32_t> CombinedMetadata::MethodsOf(uint32_t type) const {
	auto [module, local] = Local(Token(ClrTableId::TypeDef, type));
	if (!module)
		return {};
	auto methods = module->Md.MethodsOf(RowOf(local));
	for (auto& m : methods)
		m += module->Base[ClrTableId::MethodDef];
	return methods;
}

std::vector<uint32_t> CombinedMetadata::FieldsOf(uint32_t type) const {
	auto [module, local] = Local(Token(ClrTableId::TypeDef, type));
	if (!module)
		return {};
	auto fields = module->Md.FieldsOf(RowOf(local));
	for (auto& f : fields)
		f += module->Base[ClrTableId::Field];
	return fields;
}

std::wstring CombinedMetadata::TypeName(uint32_t token) const {
	auto [module, local] = Local(token);
	return module ? module->Md.TypeName(local) : std::wstring();
}

std::wstring CombinedMetadata::TypeKind(uint32_t type) const {
	auto [module, local] = Local(Token(ClrTableId::TypeDef, type));
	return module ? module->Md.TypeKind(RowOf(local)) : std::wstring();
}

std::pair<CombinedMetadata const*, uint32_t> CombinedMetadata::ResolveType(uint32_t token) const {
	if (TableOf(token) == ClrTableId::TypeDef)
		return RowOf(token) && RowOf(token) <= m_Types.size() ? std::pair{ this, RowOf(token) } : std::pair<CombinedMetadata const*, uint32_t>{};
	// (a reference: to a type of these files, if it is one)
	auto [module, local] = Local(token);
	if (!module)
		return {};
	auto resolved = TypeToken(*module, local);
	if (TableOf(resolved) == ClrTableId::TypeDef)
		return { this, RowOf(resolved) };
	// a module's own reference to its own type
	auto [owner, row] = module->Md.ResolveType(local);
	if (owner == &module->Md && row)
		return { this, row + module->Base[ClrTableId::TypeDef] };
	return {};
}

std::vector<uint32_t> CombinedMetadata::Interfaces(uint32_t type) const {
	auto [module, local] = Local(Token(ClrTableId::TypeDef, type));
	if (!module)
		return {};
	auto tokens = module->Md.Interfaces(RowOf(local));
	for (auto& t : tokens)
		t = TypeToken(*module, t);
	return tokens;
}

std::vector<ClrCustomAttribute> CombinedMetadata::CustomAttributes(uint32_t token) const {
	auto [module, local] = Local(token);
	if (!module)
		return {};
	auto attributes = module->Md.CustomAttributes(local);
	for (auto& a : attributes) {
		a.Parent = Global(*module, a.Parent);
		a.Constructor = Global(*module, a.Constructor);
		a.AttributeType = Global(*module, a.AttributeType);
	}
	return attributes;
}

std::vector<uint32_t> CombinedMetadata::CustomAttributeTypes(uint32_t token) const {
	auto [module, local] = Local(token);
	if (!module)
		return {};
	auto types = module->Md.CustomAttributeTypes(local);
	for (auto& t : types)
		t = Global(*module, t);
	return types;
}

std::wstring CombinedMetadata::AttributeCSharp(ClrCustomAttribute const& attribute) const {
	auto [module, parent] = Local(attribute.Parent);
	if (!module)
		return {};
	// (in its module's tokens)
	auto local = attribute;
	local.Parent = parent;
	local.Constructor = Local(attribute.Constructor).second;
	local.AttributeType = Local(attribute.AttributeType).second;
	return module->Md.AttributeCSharp(local);
}

bool CombinedMetadata::MethodSigTypes(uint32_t method, ClrSigType& result, std::vector<ClrSigType>& params) const {
	auto [module, local] = Local(Token(ClrTableId::MethodDef, method));
	if (!module || !module->Md.MethodSigTypes(RowOf(local), result, params))
		return false;
	Rebase(*module, result);
	for (auto& p : params)
		Rebase(*module, p);
	return true;
}

bool CombinedMetadata::FieldSigType(uint32_t field, ClrSigType& type) const {
	auto [module, local] = Local(Token(ClrTableId::Field, field));
	if (!module || !module->Md.FieldSigType(RowOf(local), type))
		return false;
	Rebase(*module, type);
	return true;
}

std::wstring CombinedMetadata::ParamName(uint32_t method, uint32_t sequence) const {
	auto [module, local] = Local(Token(ClrTableId::MethodDef, method));
	return module ? module->Md.ParamName(RowOf(local), sequence) : std::wstring();
}

uint16_t CombinedMetadata::ParamFlags(uint32_t method, uint32_t sequence) const {
	auto [module, local] = Local(Token(ClrTableId::MethodDef, method));
	return module ? module->Md.ParamFlags(RowOf(local), sequence) : 0;
}

uint32_t CombinedMetadata::ParamToken(uint32_t method, uint32_t sequence) const {
	auto [module, local] = Local(Token(ClrTableId::MethodDef, method));
	if (!module)
		return 0;
	auto token = module->Md.ParamToken(RowOf(local), sequence);
	return token ? Global(*module, token) : 0;
}

std::optional<ClrPInvoke> CombinedMetadata::PInvoke(uint32_t method) const {
	auto [module, local] = Local(Token(ClrTableId::MethodDef, method));
	return module ? module->Md.PInvoke(RowOf(local)) : std::nullopt;
}

std::shared_ptr<ClrEnumInfo const> CombinedMetadata::EnumInfo(uint32_t token) const {
	auto [module, local] = Local(token);
	return module ? module->Md.EnumInfo(local) : nullptr;
}

std::optional<int64_t> CombinedMetadata::ConstantInteger(uint32_t token) const {
	auto [module, local] = Local(token);
	return module ? module->Md.ConstantInteger(local) : std::nullopt;
}

std::wstring CombinedMetadata::ConstantCSharp(uint32_t token) const {
	auto [module, local] = Local(token);
	return module ? module->Md.ConstantCSharp(local) : std::wstring();
}

std::pair<uint16_t, uint32_t> CombinedMetadata::ClassLayout(uint32_t type) const {
	auto [module, local] = Local(Token(ClrTableId::TypeDef, type));
	return module ? module->Md.ClassLayout(RowOf(local)) : std::pair<uint16_t, uint32_t>{};
}

std::optional<uint32_t> CombinedMetadata::FieldOffset(uint32_t field) const {
	auto [module, local] = Local(Token(ClrTableId::Field, field));
	return module ? module->Md.FieldOffset(RowOf(local)) : std::nullopt;
}
