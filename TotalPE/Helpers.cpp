#include "pch.h"
#include "Helpers.h"
#include "Resource.h"
#include <DiaHelper.h>
#include "Interfaces.h"
#include <DpiHelper.h>

namespace {
	// a filter box of a rebar: its size (at 96 DPI) and its font
	struct FilterBand {
		int Width, Height;
		CFont Font;
	};

	void UpdateFilter(HWND hFilter, FilterBand* data, bool band) {
		auto dpi = DpiHelper::GetDpi(hFilter);
		data->Font.Attach(DpiHelper::CreateMessageFont(dpi));
		::SendMessage(hFilter, WM_SETFONT, (WPARAM)(HFONT)data->Font, TRUE);
		if (!band)
			return;
		CReBarCtrl rebar(::GetParent(hFilter));
		REBARBANDINFO rbbi{ sizeof(rbbi), RBBIM_CHILD };
		for (UINT i = 0; i < rebar.GetBandCount(); i++)
			if (rebar.GetBandInfo(i, &rbbi) && rbbi.hwndChild == hFilter) {
				// (as WTL's AddSimpleReBarBandCtrl has it)
				rbbi.fMask = RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_IDEALSIZE;
				rbbi.cxMinChild = 0;
				rbbi.cyMinChild = DpiHelper::Scale(data->Height, dpi);
				rbbi.cx = rbbi.cxIdeal = DpiHelper::Scale(data->Width, dpi);
				rebar.SetBandInfo(i, &rbbi);
				// (the view lays itself out again: the rebar may be of another height)
				auto view = rebar.GetParent();
				CRect client;
				view.GetClientRect(&client);
				view.SendMessage(WM_SIZE, SIZE_RESTORED, MAKELPARAM(client.Width(), client.Height()));
				break;
			}
	}

	LRESULT CALLBACK FilterProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR refData) {
		auto data = reinterpret_cast<FilterBand*>(refData);
		switch (msg) {
			case WM_DPICHANGED_AFTERPARENT:
			{
				// (the font before is deleted once the box has the new one)
				CFont old(data->Font.Detach());
				auto result = ::DefSubclassProc(hWnd, msg, wp, lp);
				UpdateFilter(hWnd, data, true);
				return result;
			}
			case WM_NCDESTROY:
				::RemoveWindowSubclass(hWnd, FilterProc, id);
				delete data;
				break;
		}
		return ::DefSubclassProc(hWnd, msg, wp, lp);
	}
}

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

void Helpers::FillTreeListView(IMainFrame* frame, CTreeListViewCtrl& tv, HTLITEM hRoot, DiaSymbol const& sym, DiaSession const& session, PVOID address) {
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
		auto hItem = tv.InsertItem(hRoot, TLV_LAST, member.Name().c_str(), image);
		tv.SetItemText(hItem, 2, member.TypeName().c_str());
		tv.SetItemText(hItem, 1, std::format(L"+{}    ", member.Offset()).c_str());

		if (address) {
			ULONG64 value = 0;
			if (member.Location() == LocationKind::BitField) {
				if (ReadVirtual((PBYTE)address + member.Offset(), (ULONG)type.Length(), &value)) {
					tv.SetItemText(hItem, 3, std::format(L"0x{:X}", (value >> member.BitPosition()) & ((1 << (ULONG)member.Length()) - 1)).c_str());
				}
			}
			else if (type.Simple() != SimpleType::NoType || type.Tag() == SymbolTag::Enum || type.Tag() == SymbolTag::PointerType) {
				ATLASSERT(type.Length());
				if (ReadVirtual((PBYTE)address + member.Offset(), (ULONG)type.Length(), &value))
					tv.SetItemText(hItem, 3, FormatSimpleValue(&value, (ULONG)type.Length(), type.Simple()).c_str());
			}
		}

		switch (type.Tag()) {
			case SymbolTag::UDT:
				if (address) {
					bool isString;
					auto value = GetSpecialValue(member, type, address, isString);
					if (!value.empty())
						tv.SetItemText(hItem, 3, value.c_str());
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
					auto hChild = tv.InsertItem(hItem, TLV_LAST, std::format(L"[{}]", i).c_str(), iconArray);
					tv.SetItemText(hChild, 1, std::format(L"+{}    ", i * elementSize).c_str());
					tv.SetItemText(hChild, 2, valueType.Name().c_str());
					if (auto simple = valueType.Simple(); simple != SimpleType::NoType) {
						ReadVirtual((PBYTE)address + offset + elementSize * i, (ULONG)elementSize, &value);
						tv.SetItemText(hChild, 3, FormatSimpleValue(&value, (ULONG)elementSize, simple).c_str());
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

void Helpers::FilterFollowsDpi(HWND hFilter, int width, int height) {
	auto data = new FilterBand{ width, height };
	UpdateFilter(hFilter, data, false);
	if (!::SetWindowSubclass(hFilter, FilterProc, 1, (DWORD_PTR)data))
		delete data;
}
