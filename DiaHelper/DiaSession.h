#pragma once

#include "dia2.h"
#include "DiaSymbol.h"
#include <atlcomcli.h>
#include <vector>
#include <string>

class DiaSession : public IDiaLoadCallback {
public:
	bool OpenImage(PCWSTR path);
	bool OpenPdb(PCWSTR path);
	void Close();
	std::wstring LastError() const;
	operator bool() const;

	DiaSymbol GlobalScope() const;
	std::vector<DiaSymbol> FindChildren(DiaSymbol const& parent, PCWSTR name = nullptr, SymbolTag tag = SymbolTag::Null, CompareOptions options = CompareOptions::None) const;
	// global scope
	std::vector<DiaSymbol> FindChildren(PCWSTR name = nullptr, SymbolTag tag = SymbolTag::Null, CompareOptions options = CompareOptions::None) const;

	DiaSymbol GetSymbolByRVA(DWORD rva, SymbolTag tag = SymbolTag::Null, long* disp = nullptr) const;
	DiaSymbol GetSymbolById(DWORD id) const;
	DiaSymbol GetSymbolByVA(ULONGLONG va, SymbolTag tag = SymbolTag::Null, long* disp = nullptr) const;

	std::wstring const& GetSymbolFile() const;

	void SetSymbolPath(PCWSTR path);
	std::wstring const& AppendSymbolPath(PCWSTR path);

private:
	// Inherited via IDiaLoadCallback
	HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override;
	ULONG __stdcall AddRef(void) override;
	ULONG __stdcall Release(void) override;
	HRESULT __stdcall NotifyDebugDir(BOOL fExecutable, DWORD cbData, BYTE* pbData) override;
	HRESULT __stdcall NotifyOpenDBG(LPCOLESTR dbgPath, HRESULT resultCode) override;
	HRESULT __stdcall NotifyOpenPDB(LPCOLESTR pdbPath, HRESULT resultCode) override;
	HRESULT __stdcall RestrictRegistryAccess(void) override;
	HRESULT __stdcall RestrictSymbolServerAccess(void) override;

	bool OpenCommon(PCWSTR path, bool image);

	enum class SymbolsFileType {
		Dbg,
		Pdb
	};
	CComPtr<IDiaSession> m_spSession;
	CComPtr<IDiaDataSource> m_spSource;
	std::wstring m_SymbolsFile;
	SymbolsFileType m_SymbolsFileType;
	std::wstring m_SymbolPath;
	bool m_DebugDir{ false };
};

