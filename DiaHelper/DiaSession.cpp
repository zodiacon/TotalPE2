// DiaHelper.cpp : Defines the functions for the static library.
//

#include "pch.h"
#include "DiaHelper.h"

HMODULE g_hDiaDll;

bool DiaSession::OpenImage(PCWSTR path) {
	return OpenCommon(path, true);
}

bool DiaSession::OpenPdb(PCWSTR path) {
	return OpenCommon(path, false);
}

void DiaSession::Close() {
	m_spSession.Release();
}

DiaSession::operator bool() const {
	return m_spSession != nullptr;
}

std::wstring DiaSession::LastError() const {
	CComBSTR text;
	return m_spSource && S_OK == m_spSource->get_lastError(&text) ? text.m_str : L"";
}

DiaSymbol DiaSession::GlobalScope() const {
	if (m_spSession == nullptr)
		return DiaSymbol(nullptr);

	CComPtr<IDiaSymbol> spSym;
	m_spSession->get_globalScope(&spSym);
	return DiaSymbol(spSym);
}

std::vector<DiaSymbol> DiaSession::FindChildren(DiaSymbol const& parent, PCWSTR name, SymbolTag tag, CompareOptions options) const {
	std::vector<DiaSymbol> symbols;
	CComPtr<IDiaEnumSymbols> spEnum;
	if (SUCCEEDED(m_spSession->findChildren(parent.m_spSym, (enum SymTagEnum)tag, name, (DWORD)options, &spEnum))) {
		LONG count = 0;
		spEnum->get_Count(&count);
		ULONG ret;
		for (LONG i = 0; i < count; i++) {
			CComPtr<IDiaSymbol> sym;
			spEnum->Next(1, &sym, &ret);
			ATLASSERT(sym);
			if (sym == nullptr)
				break;
			symbols.push_back(DiaSymbol(sym));
		}
	}
	
	return symbols;
}

std::vector<DiaSymbol> DiaSession::FindChildren(PCWSTR name, SymbolTag tag, CompareOptions options) const {
	return FindChildren(GlobalScope(), name, tag, options);
}

DiaSymbol DiaSession::GetSymbolByRVA(DWORD rva, SymbolTag tag) const {
	CComPtr<IDiaSymbol> spSym;
	m_spSession->findSymbolByRVA(rva, static_cast<enum SymTagEnum>(tag), &spSym);
	return DiaSymbol(spSym);
}

DiaSymbol DiaSession::GetSymbolById(DWORD id) const {
	CComPtr<IDiaSymbol> spSym;
	m_spSession->symbolById(id, &spSym);
	return DiaSymbol(spSym);
}

std::wstring const& DiaSession::GetSymbolFile() const {
	return m_SymbolsFile;
}

void DiaSession::SetSymbolPath(PCWSTR path) {
	m_SymbolPath = path;
}

std::wstring const& DiaSession::AppendSymbolPath(PCWSTR path) {
	m_SymbolPath += path;
	return m_SymbolPath;
}

bool DiaSession::OpenCommon(PCWSTR path, bool image) {
	if (g_hDiaDll == nullptr) {
		WCHAR path[MAX_PATH];
		if (::GetModuleFileName(nullptr, path, _countof(path))) {
			auto bs = wcsrchr(path, L'\\');
			*bs = 0;
			wcscat_s(path, L"\\msdia140.dll");
			g_hDiaDll = ::LoadLibrary(path);
		}
	}
	CComPtr<IDiaDataSource> spSource;
	HRESULT hr;
	if (g_hDiaDll) {
		//
		// implement CoCreateInstance manually to make sure our msdia DLL is loaded
		//
		auto dgco = (decltype(::DllGetClassObject)*)::GetProcAddress(g_hDiaDll, "DllGetClassObject");
		if (!dgco)
			return false;

		CComPtr<IClassFactory> spCF;
		if (FAILED(dgco(__uuidof(DiaSource), __uuidof(IClassFactory), reinterpret_cast<void**>(&spCF))))
			return false;

		hr = spCF->CreateInstance(nullptr, __uuidof(IDiaDataSource), reinterpret_cast<void**>(&spSource));
	}
	else {
		hr = spSource.CoCreateInstance(__uuidof(DiaSource));
	}
	if (FAILED(hr))
		return false;
	if (image)
		hr = spSource->loadDataForExe(path, m_SymbolPath.c_str(), this);
	else
		hr = spSource->loadDataFromPdb(path);
	if (FAILED(hr))
		return false;

	CComPtr<IDiaSession> spSession;
	hr = spSource->openSession(&spSession);
	if (FAILED(hr))
		return false;

	m_spSession = spSession;
	m_spSource = spSource;
	return true;
}

HRESULT __stdcall DiaSession::QueryInterface(REFIID riid, void** ppvObject) {
	if (riid == __uuidof(IUnknown) || riid == __uuidof(IDiaLoadCallback)) {
		*ppvObject = static_cast<IDiaLoadCallback*>(this);
		return S_OK;
	}
	return E_NOINTERFACE;
}

ULONG __stdcall DiaSession::AddRef(void) {
	return 2;
}

ULONG __stdcall DiaSession::Release(void) {
	return 1;
}

HRESULT __stdcall DiaSession::NotifyDebugDir(BOOL fExecutable, DWORD cbData, BYTE* pbData) {
	m_DebugDir = true;
	return S_OK;
}

HRESULT __stdcall DiaSession::NotifyOpenDBG(LPCOLESTR dbgPath, HRESULT resultCode) {
	if (resultCode == S_OK) {
		m_SymbolsFile = dbgPath;
		m_SymbolsFileType = SymbolsFileType::Dbg;
	}
	return S_OK;
}

HRESULT __stdcall DiaSession::NotifyOpenPDB(LPCOLESTR pdbPath, HRESULT resultCode) {
	if (resultCode == S_OK) {
		m_SymbolsFile = pdbPath;
		m_SymbolsFileType = SymbolsFileType::Pdb;
	}
	return S_OK;
}

HRESULT __stdcall DiaSession::RestrictRegistryAccess(void) {
	return S_OK;
}

HRESULT __stdcall DiaSession::RestrictSymbolServerAccess(void) {
	return S_OK;
}
