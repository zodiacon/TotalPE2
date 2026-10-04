#include "pch.h"
#include "TypeLibText.h"
#include <ole2.h>
#include <atlbase.h>
#include <atlcomcli.h>

#pragma comment(lib, "oleaut32")

namespace {
	std::wstring GuidToString(GUID const& guid) {
		WCHAR text[64]{};
		::StringFromGUID2(guid, text, _countof(text));
		std::wstring s(text);
		// IDL writes the GUID without braces
		if (s.size() > 2)
			s = s.substr(1, s.size() - 2);
		return s;
	}

	std::wstring Escape(BSTR text) {
		std::wstring s;
		for (auto p = text; p && *p; p++) {
			if (*p == L'"' || *p == L'\\')
				s += L'\\';
			s += *p;
		}
		return s;
	}

	std::wstring TypeInfoName(ITypeInfo* info) {
		CComBSTR name;
		if (info && SUCCEEDED(info->GetDocumentation(MEMBERID_NIL, &name, nullptr, nullptr, nullptr)) && name)
			return (PCWSTR)name;
		return L"?";
	}

	std::wstring TypeToString(ITypeInfo* info, TYPEDESC const& desc) {
		switch (desc.vt) {
			case VT_PTR: return TypeToString(info, *desc.lptdesc) + L"*";
			case VT_SAFEARRAY: return L"SAFEARRAY(" + TypeToString(info, *desc.lptdesc) + L")";
			case VT_CARRAY:
			{
				auto text = TypeToString(info, desc.lpadesc->tdescElem);
				for (USHORT i = 0; i < desc.lpadesc->cDims; i++)
					text += std::format(L"[{}]", desc.lpadesc->rgbounds[i].cElements);
				return text;
			}
			case VT_USERDEFINED:
			{
				CComPtr<ITypeInfo> ref;
				if (SUCCEEDED(info->GetRefTypeInfo(desc.hreftype, &ref)))
					return TypeInfoName(ref);
				return L"?";
			}
			case VT_EMPTY: return L"void";
			case VT_VOID: return L"void";
			case VT_I1: return L"char";
			case VT_UI1: return L"unsigned char";
			case VT_I2: return L"short";
			case VT_UI2: return L"unsigned short";
			case VT_I4: return L"long";
			case VT_UI4: return L"unsigned long";
			case VT_I8: return L"int64";
			case VT_UI8: return L"uint64";
			case VT_INT: return L"int";
			case VT_UINT: return L"unsigned int";
			case VT_R4: return L"float";
			case VT_R8: return L"double";
			case VT_CY: return L"CURRENCY";
			case VT_DATE: return L"DATE";
			case VT_BSTR: return L"BSTR";
			case VT_DISPATCH: return L"IDispatch*";
			case VT_UNKNOWN: return L"IUnknown*";
			case VT_ERROR: return L"SCODE";
			case VT_BOOL: return L"VARIANT_BOOL";
			case VT_VARIANT: return L"VARIANT";
			case VT_DECIMAL: return L"DECIMAL";
			case VT_HRESULT: return L"HRESULT";
			case VT_LPSTR: return L"LPSTR";
			case VT_LPWSTR: return L"LPWSTR";
			case VT_INT_PTR: return L"INT_PTR";
			case VT_UINT_PTR: return L"UINT_PTR";
			case VT_CLSID: return L"GUID";
		}
		return std::format(L"VARTYPE({})", desc.vt);
	}

	std::wstring VariantToString(VARIANT const* value) {
		if (!value)
			return L"";
		CComVariant v;
		if (FAILED(::VariantChangeType(&v, value, VARIANT_ALPHABOOL, VT_BSTR)) || !v.bstrVal)
			return L"?";
		if (value->vt == VT_BSTR)
			return L"\"" + Escape(v.bstrVal) + L"\"";
		return (PCWSTR)v.bstrVal;
	}

	std::wstring ParamAttributes(PARAMDESC const& p) {
		std::wstring attrs;
		auto add = [&](PCWSTR text) {
			if (!attrs.empty())
				attrs += L", ";
			attrs += text;
		};
		if (p.wParamFlags & PARAMFLAG_FIN) add(L"in");
		if (p.wParamFlags & PARAMFLAG_FOUT) add(L"out");
		if (p.wParamFlags & PARAMFLAG_FLCID) add(L"lcid");
		if (p.wParamFlags & PARAMFLAG_FRETVAL) add(L"retval");
		if (p.wParamFlags & PARAMFLAG_FOPT) add(L"optional");
		if ((p.wParamFlags & PARAMFLAG_FHASDEFAULT) && p.pparamdescex)
			attrs += (attrs.empty() ? L"" : L", ") + std::format(L"defaultvalue({})", VariantToString(&p.pparamdescex->varDefaultValue));
		return attrs.empty() ? L"" : L"[" + attrs + L"] ";
	}

	std::wstring HelpString(ITypeInfo* info, MEMBERID id) {
		CComBSTR doc;
		if (SUCCEEDED(info->GetDocumentation(id, nullptr, &doc, nullptr, nullptr)) && doc && doc.Length())
			return std::format(L"helpstring(\"{}\")", Escape(doc));
		return L"";
	}

	void DescribeFunctions(ITypeInfo* info, TYPEATTR const* attr, std::wstring& out) {
		for (WORD f = 0; f < attr->cFuncs; f++) {
			FUNCDESC* func;
			if (FAILED(info->GetFuncDesc(f, &func)))
				continue;
			std::vector<BSTR> names(func->cParams + 1);
			UINT count = 0;
			info->GetNames(func->memid, names.data(), (UINT)names.size(), &count);

			std::wstring attrs = std::format(L"id(0x{:08X})", (uint32_t)func->memid);
			if (func->invkind == INVOKE_PROPERTYGET) attrs += L", propget";
			if (func->invkind == INVOKE_PROPERTYPUT) attrs += L", propput";
			if (func->invkind == INVOKE_PROPERTYPUTREF) attrs += L", propputref";
			if (func->wFuncFlags & FUNCFLAG_FRESTRICTED) attrs += L", restricted";
			if (func->wFuncFlags & FUNCFLAG_FHIDDEN) attrs += L", hidden";
			if (auto help = HelpString(info, func->memid); !help.empty())
				attrs += L", " + help;

			out += std::format(L"    [{}]\r\n    {} {}(", attrs, TypeToString(info, func->elemdescFunc.tdesc), count ? names[0] : L"?");
			for (SHORT p = 0; p < func->cParams; p++) {
				auto const& param = func->lprgelemdescParam[p];
				// a property put has no name for its value
				PCWSTR name = (UINT)(p + 1) < count ? names[p + 1] : (func->invkind & (INVOKE_PROPERTYPUT | INVOKE_PROPERTYPUTREF) ? L"value" : L"");
				out += std::format(L"{}{}{} {}", p ? L", " : L"", ParamAttributes(param.paramdesc), TypeToString(info, param.tdesc), name);
			}
			out += L");\r\n";
			for (UINT i = 0; i < count; i++)
				::SysFreeString(names[i]);
			info->ReleaseFuncDesc(func);
		}
	}

	void DescribeVariables(ITypeInfo* info, TYPEATTR const* attr, std::wstring& out, bool constants) {
		for (WORD v = 0; v < attr->cVars; v++) {
			VARDESC* var;
			if (FAILED(info->GetVarDesc(v, &var)))
				continue;
			CComBSTR name;
			UINT count;
			info->GetNames(var->memid, &name, 1, &count);
			if (constants)
				out += std::format(L"    {} = {},\r\n", (PCWSTR)name, var->varkind == VAR_CONST ? VariantToString(var->lpvarValue) : L"?");
			else
				out += std::format(L"    {} {};\r\n", TypeToString(info, var->elemdescVar.tdesc), (PCWSTR)name);
			info->ReleaseVarDesc(var);
		}
	}

	void DescribeType(ITypeInfo* info, std::wstring& out) {
		TYPEATTR* attr;
		if (FAILED(info->GetTypeAttr(&attr)))
			return;
		auto name = TypeInfoName(info);
		auto help = HelpString(info, MEMBERID_NIL);
		auto header = [&](PCWSTR extra = L"") {
			std::wstring attrs;
			if (attr->guid != GUID_NULL)
				attrs = L"uuid(" + GuidToString(attr->guid) + L")";
			auto append = [&](std::wstring const& text) {
				if (!text.empty())
					attrs += (attrs.empty() ? L"" : L", ") + text;
			};
			append(extra);
			append(help);
			if (!attrs.empty())
				out += L"[" + attrs + L"]\r\n";
		};

		switch (attr->typekind) {
			case TKIND_ENUM:
				out += std::format(L"typedef enum {} {{\r\n", name);
				DescribeVariables(info, attr, out, true);
				out += std::format(L"}} {};\r\n\r\n", name);
				break;

			case TKIND_RECORD:
			case TKIND_UNION:
				header();
				out += std::format(L"typedef {} {} {{\r\n", attr->typekind == TKIND_RECORD ? L"struct" : L"union", name);
				DescribeVariables(info, attr, out, false);
				out += std::format(L"}} {};\r\n\r\n", name);
				break;

			case TKIND_ALIAS:
				out += std::format(L"typedef {} {};\r\n\r\n", TypeToString(info, attr->tdescAlias), name);
				break;

			case TKIND_MODULE:
				header();
				out += std::format(L"module {} {{\r\n", name);
				DescribeFunctions(info, attr, out);
				DescribeVariables(info, attr, out, true);
				out += L"};\r\n\r\n";
				break;

			case TKIND_INTERFACE:
			case TKIND_DISPATCH:
			{
				std::wstring base;
				HREFTYPE ref;
				if (attr->cImplTypes && SUCCEEDED(info->GetRefTypeOfImplType(0, &ref))) {
					CComPtr<ITypeInfo> baseInfo;
					if (SUCCEEDED(info->GetRefTypeInfo(ref, &baseInfo)))
						base = L" : " + TypeInfoName(baseInfo);
				}
				std::wstring flags = L"object";
				if (attr->wTypeFlags & TYPEFLAG_FDUAL) flags += L", dual";
				if (attr->wTypeFlags & TYPEFLAG_FOLEAUTOMATION) flags += L", oleautomation";
				if (attr->wTypeFlags & TYPEFLAG_FHIDDEN) flags += L", hidden";
				header(flags.c_str());
				out += std::format(L"{} {}{} {{\r\n", attr->typekind == TKIND_DISPATCH ? L"dispinterface" : L"interface", name,
					attr->typekind == TKIND_DISPATCH ? L"" : base);
				DescribeFunctions(info, attr, out);
				if (attr->typekind == TKIND_DISPATCH)
					DescribeVariables(info, attr, out, false);
				out += L"};\r\n\r\n";
				break;
			}

			case TKIND_COCLASS:
				header(attr->wTypeFlags & TYPEFLAG_FCANCREATE ? L"" : L"noncreatable");
				out += std::format(L"coclass {} {{\r\n", name);
				for (UINT i = 0; i < attr->cImplTypes; i++) {
					HREFTYPE ref;
					INT implFlags = 0;
					CComPtr<ITypeInfo> impl;
					if (FAILED(info->GetRefTypeOfImplType(i, &ref)) || FAILED(info->GetRefTypeInfo(ref, &impl)))
						continue;
					info->GetImplTypeFlags(i, &implFlags);
					std::wstring flagsText;
					if (implFlags & IMPLTYPEFLAG_FDEFAULT) flagsText += L"default";
					if (implFlags & IMPLTYPEFLAG_FSOURCE) flagsText += flagsText.empty() ? L"source" : L", source";
					TYPEATTR* implAttr;
					bool disp = false;
					if (SUCCEEDED(impl->GetTypeAttr(&implAttr))) {
						disp = implAttr->typekind == TKIND_DISPATCH;
						impl->ReleaseTypeAttr(implAttr);
					}
					out += std::format(L"    {}{} {};\r\n", flagsText.empty() ? L"" : L"[" + flagsText + L"] ", disp ? L"dispinterface" : L"interface", TypeInfoName(impl));
				}
				out += L"};\r\n\r\n";
				break;
		}
		info->ReleaseTypeAttr(attr);
	}
}

std::wstring DescribeTypeLib(std::span<const std::byte> data, std::wstring& error) {
	// OLE loads type libraries from files only
	WCHAR dir[MAX_PATH], path[MAX_PATH];
	if (!::GetTempPath(_countof(dir), dir) || !::GetTempFileName(dir, L"tpe", 0, path)) {
		error = L"Failed to create a temporary file";
		return {};
	}
	{
		wil::unique_hfile hFile(::CreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr));
		DWORD written;
		if (!hFile || !::WriteFile(hFile.get(), data.data(), (DWORD)data.size(), &written, nullptr)) {
			::DeleteFile(path);
			error = L"Failed to write a temporary file";
			return {};
		}
	}

	std::wstring out;
	{
		CComPtr<ITypeLib> lib;
		auto hr = ::LoadTypeLibEx(path, REGKIND_NONE, &lib);
		if (FAILED(hr)) {
			error = std::format(L"Not a type library that can be loaded (0x{:08X})", (uint32_t)hr);
		}
		else {
			CComBSTR name, doc;
			lib->GetDocumentation(MEMBERID_NIL, &name, &doc, nullptr, nullptr);
			TLIBATTR* attr;
			if (SUCCEEDED(lib->GetLibAttr(&attr))) {
				out += std::format(L"[\r\n    uuid({}),\r\n    version({}.{})", GuidToString(attr->guid), attr->wMajorVerNum, attr->wMinorVerNum);
				if (attr->lcid)
					out += std::format(L",\r\n    lcid(0x{:X})", attr->lcid);
				lib->ReleaseTLibAttr(attr);
			}
			if (doc && doc.Length())
				out += std::format(L",\r\n    helpstring(\"{}\")", Escape(doc));
			out += std::format(L"\r\n]\r\nlibrary {}\r\n{{\r\n\r\n", name ? (PCWSTR)name : L"?");

			auto count = lib->GetTypeInfoCount();
			for (UINT i = 0; i < count; i++) {
				CComPtr<ITypeInfo> info;
				if (SUCCEEDED(lib->GetTypeInfo(i, &info)))
					DescribeType(info, out);
			}
			out += L"};\r\n";
		}
	}
	::DeleteFile(path);
	return out;
}
