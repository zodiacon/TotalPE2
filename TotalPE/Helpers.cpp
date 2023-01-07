#include "pch.h"
#include "Helpers.h"
#include "Resource.h"
#include <DiaHelper.h>
#include "TreeListView.h"
#include "Interfaces.h"

bool Helpers::ExtractResource(HMODULE hModule, UINT resId, PCWSTR type, PCWSTR path, bool overwrite) {
	auto hResource = ::FindResource(hModule, MAKEINTRESOURCE(resId), type);
	if (!hResource)
		return false;

	bool ok = false;
	auto hGlobal = ::LoadResource(hModule, hResource);
	if (hGlobal) {
		auto size = ::SizeofResource(hModule, hResource);
		if (size) {
			auto p = ::LockResource(hGlobal);
			if (p) {
				wil::unique_hfile hFile(::CreateFile(path, GENERIC_WRITE, 0, nullptr, overwrite ? OPEN_ALWAYS : CREATE_ALWAYS, 0, nullptr));
				if (hFile) {
					DWORD written;
					ok = ::WriteFile(hFile.get(), p, size, &written, nullptr);
				}
			}
		}
	}

	return ok;
}

bool Helpers::ExtractModules() {
	WCHAR path[MAX_PATH];
	::GetModuleFileName(nullptr, path, _countof(path));
	*wcsrchr(path, L'\\') = 0;
	std::wstring spath(path);

	auto ok = ExtractResource(nullptr, IDR_DIA, L"BIN", (spath + L"\\msdia140.dll").c_str(), false);
	ok |= ExtractResource(nullptr, IDR_SYMSRV, L"BIN", (spath + L"\\symsrv.dll").c_str(), false);
	return ok;
}

ULONG Helpers::ReadVirtual(PVOID address, ULONG size, PVOID buffer) {
	memcpy(buffer, address, size);
	return size;
}

std::wstring Helpers::ValueToString(DiaSymbol const& member, PVOID address) {
	ULONG64 value = 0;
	auto type = member.Type();
	if (member.Location() == LocationKind::BitField) {
		if (ReadVirtual((PBYTE)address + member.Offset(), (ULONG)type.Length(), &value)) {
			return std::format(L"0x{:X}", (value >> member.BitPosition()) & ((1 << (ULONG)member.Length()) - 1));
		}
	}
	else if (type.Simple() != SimpleType::NoType || type.Tag() == SymbolTag::Enum || type.Tag() == SymbolTag::PointerType) {
		ATLASSERT(type.Length());
		if (ReadVirtual((PBYTE)address + member.Offset(), (ULONG)type.Length(), &value))
			return std::format(L"0x{:X}", value);
	}
	else if (member.Location() == LocationKind::BitField) {
		if (ReadVirtual((PBYTE)address + member.Offset(), (ULONG)type.Length(), &value)) {
			return std::format(L"0x{:X}", (value >> member.BitPosition()) & ((1 << (ULONG)member.Length()) - 1));
		}
	}
	else if (type.Simple() != SimpleType::NoType || type.Tag() == SymbolTag::Enum || type.Tag() == SymbolTag::PointerType) {
		ATLASSERT(type.Length());
		if (ReadVirtual((PBYTE)address + member.Offset(), (ULONG)type.Length(), &value))
			return std::format(L"0x{:X}", value);
	}
	return L"";
}

std::wstring Helpers::FormatSimpleValue(PVOID value, ULONG size, SimpleType type) {
	switch (type) {
		case SimpleType::Int: return std::format(L"0x{:X} ({})", *(int64_t*)value, *(int64_t*)value);
		case SimpleType::UInt: return std::format(L"0x{:X} ({})", *(uint64_t*)value, *(uint64_t*)value);
		case SimpleType::Int4B: return std::format(L"0x{:X} ({})", *(int64_t*)value, *(int64_t*)value);
		case SimpleType::UInt4B: return std::format(L"0x{:X} ({})", *(uint64_t*)value, *(uint64_t*)value);
		case SimpleType::Float: return std::format(L"{}", size == sizeof(float) ? *(float*)value : *(double*)value);
		case SimpleType::BSTR: return (BSTR)value;
	}
	return L"";
}

void Helpers::FillTreeListView(IMainFrame* frame, CTreeListView& tv, HTREEITEM hRoot, DiaSymbol const& sym, DiaSession const& session, PVOID address) {
	static int iconBitField = frame->GetIconIndex(IDI_BITFIELD);
	static int iconUnion = frame->GetIconIndex(IDI_UNION);
	static int iconStruct = frame->GetIconIndex(IDI_TYPE);
	static int iconArray = frame->GetIconIndex(IDI_ARRAY);
	static int iconEnum = frame->GetIconIndex(IDI_ENUM);
	static int iconField = frame->GetIconIndex(IDI_FIELD);

	for (auto member : sym.FindChildren()) {
		auto type = member.Type();
		int image = iconField;
		if (member.Location() == LocationKind::BitField)
			image = iconBitField;
		else {
			switch (type.Tag()) {
				case SymbolTag::UDT:
					image = member.Type().UdtKind() == UdtType::Union ? iconUnion : iconStruct;
					break;
				case SymbolTag::Enum:
					image = iconEnum;
					break;
				case SymbolTag::ArrayType:
					image = iconArray;
					break;

			}
		}
		auto hItem = tv.GetTreeControl().InsertItem(std::format(L"{}", member.Name()).c_str(), image, image, hRoot, TVI_LAST);
		tv.SetSubItemText(hItem, 2, member.TypeName().c_str());
		tv.SetSubItemText(hItem, 1, std::format(L"+{}    ", member.Offset()).c_str(), TLVIFMT_RIGHT);

		if (address) {
			ULONG64 value = 0;
			if (member.Location() == LocationKind::BitField) {
				if (ReadVirtual((PBYTE)address + member.Offset(), (ULONG)type.Length(), &value)) {
					tv.SetSubItemText(hItem, 3, std::format(L"0x{:X}", (value >> member.BitPosition()) & ((1 << (ULONG)member.Length()) - 1)).c_str(), TLVIFMT_RIGHT);
				}
			}
			else if (type.Simple() != SimpleType::NoType || type.Tag() == SymbolTag::Enum || type.Tag() == SymbolTag::PointerType) {
				ATLASSERT(type.Length());
				if (ReadVirtual((PBYTE)address + member.Offset(), (ULONG)type.Length(), &value))
					tv.SetSubItemText(hItem, 3, FormatSimpleValue(&value, (ULONG)type.Length(), type.Simple()).c_str(), TLVIFMT_RIGHT);
			}
		}

		switch (type.Tag()) {
			case SymbolTag::UDT:
				if (address) {
					bool isString;
					auto value = GetSpecialValue(member, type, address, isString);
					if (!value.empty())
						tv.SetSubItemText(hItem, 3, value.c_str(), isString ? TLVIFMT_LEFT : TLVIFMT_RIGHT);
				}
				FillTreeListView(frame, tv, hItem, type, session, address == nullptr ? nullptr : (PBYTE)address + member.Offset());
				break;

			case SymbolTag::ArrayType:
				//
				// expand to array contents
				//
				auto count = type.Count();
				auto elementSize = type.Length() / count;
				auto valueType = type.Type();
				auto offset = member.Offset();
				ULONGLONG value = 0;
				for (uint32_t i = 0; i < count; i++) {
					auto hChild = tv.GetTreeControl().InsertItem(std::format(L"[{}]", i).c_str(), iconArray, iconArray, hItem, TVI_LAST);
					tv.SetSubItemText(hChild, 1, std::format(L"+{}    ", i * elementSize).c_str(), TLVIFMT_RIGHT);
					tv.SetSubItemText(hChild, 2, valueType.Name().c_str());
					if (auto simple = valueType.Simple(); simple != SimpleType::NoType) {
						ReadVirtual((PBYTE)address + offset + elementSize * i, (ULONG)elementSize, &value);
						tv.SetSubItemText(hChild, 3, FormatSimpleValue(&value, (ULONG)elementSize, simple).c_str(), TLVIFMT_RIGHT);
					}
					FillTreeListView(frame, tv, hChild, valueType, session, (PBYTE)address + member.Offset() + elementSize * i);
				}
				break;
		}
	}
}

std::wstring Helpers::GetSpecialValue(DiaSymbol const& field, DiaSymbol const& type, PVOID address, bool& isString) {
	isString = false;
	auto typeName = field.TypeName();
	if (typeName == L"_LARGE_INTEGER") {
		auto value = *(ULONG64*)((PBYTE)address + field.Offset());
		return std::format(L"0x{:X}", value);
	}
	return L"";
}
