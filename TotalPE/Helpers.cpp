#include "pch.h"
#include "Helpers.h"
#include "Resource.h"
#include <DiaHelper.h>
#include "TreeListView.h"

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
	return L"";
}

std::wstring Helpers::FormatSimpleValue(PVOID value, ULONG size, SimpleType type) {
	switch (type) {
		case SimpleType::Int: return std::format(L"{}", *(int64_t*)value);
		case SimpleType::UInt: return std::format(L"{}", *(uint64_t*)value);
	}
	return L"";
}

void Helpers::FillTreeListView(CTreeListView& tv, HTREEITEM hRoot, DiaSymbol const& sym, DiaSession const& session, PVOID address) {
	for (auto member : sym.FindChildren()) {
		int image = 2;
		if (member.Location() == LocationKind::BitField)
			image = 4;
		else {
			switch (member.Type().Tag()) {
				case SymbolTag::UDT:
					image = member.Type().UdtKind() == UdtType::Union ? 1 : 0;
					break;
				case SymbolTag::Enum:
					image = 3;
					break;
			}
		}
		auto hItem = tv.GetTreeControl().InsertItem(std::format(L"{}", member.Name()).c_str(),
			image, image, hRoot, TVI_LAST);
		tv.SetSubItemText(hItem, 2, member.TypeName().c_str());
		tv.SetSubItemText(hItem, 1, std::format(L"+{}    ", member.Offset()).c_str(), TLVIFMT_RIGHT);

		auto type = member.Type();

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
					tv.SetSubItemText(hItem, 3, std::format(L"0x{:X}", value).c_str(), TLVIFMT_RIGHT);
			}
		}

		if (type.Tag() == SymbolTag::UDT) {
			if (address) {
				bool isString;
				auto value = GetSpecialValue(member, type, address, isString);
				if (!value.empty())
					tv.SetSubItemText(hItem, 3, value.c_str(), isString ? TLVIFMT_LEFT : TLVIFMT_RIGHT);
			}
			FillTreeListView(tv, hItem, type, session, address == nullptr ? nullptr : (PBYTE)address + member.Offset());
		}
		else if (type.Tag() == SymbolTag::ArrayType) {
			//
			// expand to array contents
			//
			auto count = type.Count();
			auto elementSize = type.Length() / count;
			auto valueType = type.Type();
			auto offset = member.Offset();
			ULONGLONG value = 0;
			for (uint32_t i = 0; i < count; i++) {
				auto hChild = tv.GetTreeControl().InsertItem(std::format(L"[{}]", i).c_str(), hItem, TVI_LAST);
				tv.SetSubItemText(hChild, 1, std::format(L"+{}    ", i * elementSize).c_str(), TLVIFMT_RIGHT);
				tv.SetSubItemText(hChild, 2, valueType.Name().c_str());
				if (auto simple = valueType.Simple(); simple != SimpleType::NoType) {
					ReadVirtual((PBYTE)address + offset + elementSize * i, (ULONG)elementSize, &value);
					tv.SetSubItemText(hChild, 3, FormatSimpleValue(&value, (ULONG)elementSize, simple).c_str(), TLVIFMT_RIGHT);
				}
				FillTreeListView(tv, hChild, valueType, session, (PBYTE)address + member.Offset() + elementSize * i);
			}
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
