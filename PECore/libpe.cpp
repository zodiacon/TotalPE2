/****************************************************************************************
* Copyright © 2018-2022, Jovibor: https://github.com/jovibor/                           *
* libpe is a library for obtaining PE32 (x86) and PE32+ (x64) files' inner structure.   *
* Official git repository: https://github.com/jovibor/libpe                             *
* This software is available under the "MIT License".                                   *
****************************************************************************************/
#include "pch.h"
#include "libpe.h"
#include <cassert>
#include <strsafe.h>

#define LIBPE_PRODUCT_NAME		  L"libpe, (C) Jovibor 2018-2022, https://github.com/jovibor/libpe"
#define LIBPE_VERSION_MAJOR		  1
#define LIBPE_VERSION_MINOR		  2
#define LIBPE_VERSION_MAINTENANCE 0

#define TO_WSTR_HELPER(x) L## #x
#define TO_WSTR(x) TO_WSTR_HELPER(x)

namespace libpe {
#ifdef _WIN64
	constexpr auto LIBPE_VERSION_WSTR = L"" TO_WSTR(LIBPE_VERSION_MAJOR) L"." TO_WSTR(LIBPE_VERSION_MINOR) L"." TO_WSTR(LIBPE_VERSION_MAINTENANCE) L" (x64)";
#else
	constexpr auto LIBPE_VERSION_WSTR = L"" TO_WSTR(LIBPE_VERSION_MAJOR) L"." TO_WSTR(LIBPE_VERSION_MINOR) L"." TO_WSTR(LIBPE_VERSION_MAINTENANCE);
#endif

	constexpr auto LIBPE_VERSION_ULONGLONG = static_cast<ULONGLONG>((static_cast<ULONGLONG>(LIBPE_VERSION_MAJOR) << 48)
		| (static_cast<ULONGLONG>(LIBPE_VERSION_MINOR) << 32) | (static_cast<ULONGLONG>(LIBPE_VERSION_MAINTENANCE) << 16));

	extern "C" ILIBPEAPI LIBPEINFO __cdecl GetLibInfo() {
		return { LIBPE_VERSION_WSTR, LIBPE_VERSION_ULONGLONG };
	}

	//Checking overflow at summing two DWORD_PTR.
	constexpr bool IsSumOverflow(DWORD_PTR dwFirst, DWORD_PTR dwSecond) {
		return (dwFirst + dwSecond) < dwFirst;
	}

	//Class Clibpe.
	class Clibpe final : public Ilibpe {
	public:
		auto LoadPe(LPCWSTR pwszFile) -> int override;
		auto LoadPe(std::span<const std::byte> spnFile) -> int override;
		[[nodiscard]] auto GetFileInfo()const->PEFILEINFO const* override;
		[[nodiscard]] auto IsLoaded() const -> bool override;
		[[nodiscard]] auto GetOffsetFromRVA(ULONGLONG ullRVA)const->DWORD override;
		[[nodiscard]] auto GetOffsetFromVA(ULONGLONG ullVA)const->DWORD override;
		[[nodiscard]] auto GetMSDOSHeader() -> IMAGE_DOS_HEADER* override;
		[[nodiscard]] auto GetRichHeader() -> PERICHHDR_VEC* override;
		[[nodiscard]] auto GetNTHeader() -> PENtHeader* override;
		[[nodiscard]] auto GetDataDirs() -> PEDATADIR_VEC* override;
		[[nodiscard]] auto GetSecHeaders() -> PESECHDR_VEC* override;
		[[nodiscard]] auto GetExport() -> PEExport* override;
		[[nodiscard]] auto GetImport() -> PEIMPORT_VEC* override;
		[[nodiscard]] auto GetResources() -> PEResRoot* override;
		[[nodiscard]] auto GetExceptions() -> PEEXCEPTION_VEC* override;
		[[nodiscard]] auto GetSecurity() -> PESECURITY_VEC* override;
		[[nodiscard]] auto GetRelocations() -> PERELOC_VEC* override;
		[[nodiscard]] auto GetDebug() -> PEDEBUG_VEC* override;
		[[nodiscard]] auto GetTLS() -> PETLS* override;
		[[nodiscard]] auto GetLoadConfig() -> PELoadConfig* override;
		[[nodiscard]] auto GetBoundImport() -> PEBOUNDIMPORT_VEC* override;
		[[nodiscard]] auto GetDelayImport() -> PEDELAYIMPORT_VEC* override;
		[[nodiscard]] auto GetCOMDescriptor() -> PECOMDESCRIPTOR* override;

		void Clear()override;
		void Destroy()override;
	private:
		void ClearAll();
		[[nodiscard]] auto GetImageBase()const->ULONGLONG override;
		[[nodiscard]] auto GetSecHdrFromName(LPCSTR lpszName)const->PIMAGE_SECTION_HEADER override;
		[[nodiscard]] auto GetSecHdrFromRVA(ULONGLONG ullRVA)const->PIMAGE_SECTION_HEADER override;
		[[nodiscard]] auto GetBaseAddr()const->DWORD_PTR override;
		[[nodiscard]] auto GetDataSize()const->ULONGLONG override;
		[[nodiscard]] auto GetDosPtr()const->const IMAGE_DOS_HEADER*;
		[[nodiscard]] auto GetDirEntryRVA(DWORD dwEntry)const->DWORD;
		[[nodiscard]] auto GetDirEntrySize(DWORD dwEntry)const->DWORD;
		template<typename T>
		[[nodiscard]] auto GetTData(ULONGLONG ullOffset)const->T;
		template<typename T>
		[[nodiscard]] auto IsPtrSafe(T tAddr, bool fCanReferenceBoundary = false)const->bool;
		[[nodiscard]] auto PtrToOffset(LPCVOID lp)const->DWORD;
		[[nodiscard]] auto RVAToOffset(ULONGLONG ullRVA)const->DWORD;
		[[nodiscard]] auto RVAToPtr(ULONGLONG ullRVA)const->LPVOID;
		bool ParseMSDOSHeader();
		bool ParseRichHeader();
		bool ParseNTFileOptHeader();
		bool ParseDataDirectories();
		bool ParseSectionsHeaders();
		bool ParseExport();
		bool ParseImport();
		bool ParseResources();
		bool ParseExceptions();
		bool ParseSecurity();
		bool ParseRelocations();
		bool ParseDebug();
		bool ParseArchitecture();
		bool ParseGlobalPtr();
		bool ParseTLS();
		bool ParseLCD();
		bool ParseBoundImport();
		bool ParseIAT();
		bool ParseDelayImport();
		bool ParseCOMDescriptor();
	private:
		wil::unique_mapview_ptr<const std::byte> m_ptr;
		wil::unique_handle m_map;
		bool m_fLoaded{ false };              //Flag shows PE load succession.
		std::unique_ptr<char[]> m_pEmergencyMemory{ std::make_unique<char[]>(0x8FFF) }; //Reserved 16K of memory.
		std::span<const std::byte> m_spnData; //File data.
		PIMAGE_NT_HEADERS32 m_pNTHeader32{ }; //NT header pointer for x86.
		PIMAGE_NT_HEADERS64 m_pNTHeader64{ }; //NT header pointer for x64.

		//Further structs are for client code:
		PEFILEINFO m_stFileInfo{ };           //File information.
		IMAGE_DOS_HEADER m_stMSDOSHeader{ };  //DOS Header.
		PERICHHDR_VEC m_vecRichHeader{ };     //«Rich» header.
		PENtHeader m_stNTHeader{ };              //NT header.
		PEDATADIR_VEC m_vecDataDirs{ };       //DataDirectories.
		PESECHDR_VEC m_vecSecHeaders{ };      //Sections.
		PEExport m_stExport{ };               //Export table.
		PEIMPORT_VEC m_vecImport{ };          //Import table.
		PEResRoot m_stResource{ };            //Resources.
		PEEXCEPTION_VEC m_vecException{ };    //Exceptions.
		PESECURITY_VEC m_vecSecurity{ };      //Security table.
		PERELOC_VEC m_vecRelocs{ };           //Relocations.
		PEDEBUG_VEC m_vecDebug{ };            //Debug Table.
		PETLS m_stTLS{ };                     //Thread Local Storage.
		PELoadConfig m_stLCD{ };              //LoadConfigTable.
		PEBOUNDIMPORT_VEC m_vecBoundImp{ };   //Bound import.
		PEDELAYIMPORT_VEC m_vecDelayImp{ };   //Delay import.
		PECOMDESCRIPTOR m_stCOR20Desc{ };     //COM table descriptor.
	};

	//CreateRawlibpe implementation.
	extern "C" ILIBPEAPI Ilibpe * __cdecl CreateRawlibpe() {
		return new Clibpe();
	}

	auto Clibpe::LoadPe(LPCWSTR pwszFile)->int {
		assert(pwszFile != nullptr);

		const auto hFile = CreateFileW(pwszFile, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		assert(hFile != INVALID_HANDLE_VALUE);
		if (hFile == INVALID_HANDLE_VALUE)
			return ERR_FILE_OPEN;

		LARGE_INTEGER stLI{ };
		::GetFileSizeEx(hFile, &stLI);
		assert(stLI.QuadPart >= sizeof(IMAGE_DOS_HEADER));
		if (stLI.QuadPart < sizeof(IMAGE_DOS_HEADER)) {
			CloseHandle(hFile);
			return ERR_FILE_SIZESMALL;
		}

		m_map.reset(::CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr));
		CloseHandle(hFile);
		assert(m_map);
		if (!m_map) {
			return ERR_FILE_MAPPING;
		}

		m_ptr.reset((const std::byte*)MapViewOfFile(m_map.get(), FILE_MAP_READ, 0, 0, 0));
		assert(m_ptr); //Not enough memory? File is too big?
		if (m_ptr == nullptr) {
			return ERR_FILE_MAPPING;
		}
		
		const auto ret = LoadPe({ m_ptr.get(), static_cast<std::size_t>(stLI.QuadPart) });
		return ret;
	}

	auto Clibpe::LoadPe(std::span<const std::byte> spnFile)->int {
		assert(!spnFile.empty());
		if (m_fLoaded)
			ClearAll();

		if (spnFile.size() < sizeof(IMAGE_DOS_HEADER))
			return ERR_FILE_SIZESMALL;

		m_spnData = spnFile;

		if (!ParseMSDOSHeader())
			return ERR_FILE_NODOSHDR;

		m_fLoaded = true;

		ParseRichHeader();

		if (ParseNTFileOptHeader()) { //If there is no NT header then it's pointless to parse further.
			ParseDataDirectories();
			ParseSectionsHeaders();
			ParseExport();
			ParseImport();
			ParseResources();
			ParseExceptions();
			ParseSecurity();
			ParseRelocations();
			ParseDebug();
			ParseArchitecture();
			ParseGlobalPtr();
			ParseTLS();
			ParseLCD();
			ParseBoundImport();
			ParseIAT();
			ParseDelayImport();
			ParseCOMDescriptor();
		}

		return PEOK;
	}

	auto Clibpe::IsLoaded() const -> bool {
		return m_fLoaded;
	}

	auto Clibpe::GetFileInfo()const->PEFILEINFO const* {
		if (!m_fLoaded)
			return nullptr;

		return &m_stFileInfo;
	}

	auto Clibpe::GetOffsetFromRVA(ULONGLONG ullRVA)const->DWORD {
		assert(m_fLoaded);
		if (!m_fLoaded)
			return { };

		return RVAToOffset(ullRVA);
	}

	auto Clibpe::GetOffsetFromVA(ULONGLONG ullVA)const->DWORD {
		assert(m_fLoaded);
		if (!m_fLoaded)
			return { };

		return RVAToOffset(ullVA - GetImageBase());
	}

	auto Clibpe::GetMSDOSHeader()->IMAGE_DOS_HEADER* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasDosHdr)
			return nullptr;

		return &m_stMSDOSHeader;
	}

	auto Clibpe::GetRichHeader()->PERICHHDR_VEC* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasRichHdr)
			return nullptr;

		return &m_vecRichHeader;
	}

	auto Clibpe::GetNTHeader()->PENtHeader* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasNTHdr)
			return nullptr;

		return &m_stNTHeader;
	}

	auto Clibpe::GetDataDirs()->PEDATADIR_VEC* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasDataDirs)
			return nullptr;

		return &m_vecDataDirs;
	}

	auto Clibpe::GetSecHeaders()->PESECHDR_VEC* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasSections)
			return nullptr;

		return &m_vecSecHeaders;
	}

	auto Clibpe::GetExport()->PEExport* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasExport)
			return nullptr;

		return &m_stExport;
	}

	auto Clibpe::GetImport()->PEIMPORT_VEC* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasImport)
			return nullptr;

		return &m_vecImport;
	}

	auto Clibpe::GetResources()->PEResRoot* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasResource)
			return nullptr;

		return &m_stResource;
	}

	auto Clibpe::GetExceptions()->PEEXCEPTION_VEC* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasException)
			return nullptr;

		return &m_vecException;
	}

	auto Clibpe::GetSecurity()->PESECURITY_VEC* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasSecurity)
			return nullptr;

		return &m_vecSecurity;
	}

	auto Clibpe::GetRelocations()->PERELOC_VEC* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasReloc)
			return nullptr;

		return &m_vecRelocs;
	}

	auto Clibpe::GetDebug()->PEDEBUG_VEC* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasDebug)
			return nullptr;

		return &m_vecDebug;
	}

	auto Clibpe::GetTLS()->PETLS* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasTLS)
			return nullptr;

		return &m_stTLS;
	}

	auto Clibpe::GetLoadConfig()->PELoadConfig* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasLoadCFG)
			return nullptr;

		return &m_stLCD;
	}

	auto Clibpe::GetBoundImport()->PEBOUNDIMPORT_VEC* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasBoundImp)
			return nullptr;

		return &m_vecBoundImp;
	}

	auto Clibpe::GetDelayImport()->PEDELAYIMPORT_VEC* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasDelayImp)
			return nullptr;

		return &m_vecDelayImp;
	}

	auto Clibpe::GetCOMDescriptor()->PECOMDESCRIPTOR* {
		assert(m_fLoaded);
		if (!m_fLoaded || !m_stFileInfo.HasCOMDescr)
			return nullptr;

		return &m_stCOR20Desc;
	}

	void Clibpe::Clear() {
		ClearAll();
	}

	void Clibpe::Destroy() {
		delete this;
	}

	auto Ilibpe::FlatResources(const PEResRoot& stResRoot)->PERESFLAT_VEC {
		std::size_t sTotalRes{ 0 }; //How many resources total?
		for (const auto& iterRoot : stResRoot.ResData) { //To reserve space in vector, count total amount of resources.
			const auto pResDirEntry = &iterRoot.ResDirEntry; //Level Root
			if (pResDirEntry->DataIsDirectory) {
				const auto pstResLvL2 = &iterRoot.ResLvL2;
				for (const auto& iterLvL2 : pstResLvL2->ResData) {
					const auto pResDirEntry2 = &iterLvL2.ResDirEntry; //Level 2 IMAGE_RESOURCE_DIRECTORY_ENTRY
					if (pResDirEntry2->DataIsDirectory) {
						sTotalRes += iterLvL2.ResLvL3.ResData.size(); //Level 3
					}
					else
						++sTotalRes;
				}
			}
			else
				++sTotalRes;
		}

		std::vector<PEResFlat> vecData{ };
		vecData.reserve(sTotalRes);
		for (auto& iterRoot : stResRoot.ResData) {
			PEResFlat stRes{ };
			const auto pResDirEntryRoot = &iterRoot.ResDirEntry; //Level Root IMAGE_RESOURCE_DIRECTORY_ENTRY
			if (pResDirEntryRoot->NameIsString)
				stRes.TypeStr = iterRoot.ResName;
			else
				stRes.TypeID = pResDirEntryRoot->Id;

			if (pResDirEntryRoot->DataIsDirectory) {
				for (auto& iterLvL2 : iterRoot.ResLvL2.ResData) {
					const auto pResDirEntry2 = &iterLvL2.ResDirEntry; //Level 2 IMAGE_RESOURCE_DIRECTORY_ENTRY
					if (pResDirEntry2->NameIsString)
						stRes.NameStr = iterLvL2.ResName;
					else
						stRes.NameID = pResDirEntry2->Id;

					if (pResDirEntry2->DataIsDirectory) {
						for (auto& iterLvL3 : iterLvL2.ResLvL3.ResData) {
							const auto pResDirEntry3 = &iterLvL3.ResDirEntry; //Level 3 IMAGE_RESOURCE_DIRECTORY_ENTRY
							if (pResDirEntry3->NameIsString)
								stRes.LangStr = iterLvL3.ResName;
							else
								stRes.LangID = pResDirEntry3->Id;

							stRes.Data = iterLvL3.RawResData;
							vecData.emplace_back(stRes);
						}
					}
					else {
						stRes.Data = iterLvL2.RawResData;
						vecData.emplace_back(stRes);
					}
				}
			}
			else {
				stRes.Data = iterRoot.RawResData;
				vecData.emplace_back(stRes);
			}
		}

		return vecData;
	}


	//Clibpe private methods.

	void Clibpe::ClearAll() {
		/******************************************************************************
		* Clearing all internal vectors and nullify all structs, pointers and flags.  *
		* Called if LoadPe is invoked second time by the same Ilibpe pointer.         *
		******************************************************************************/
		m_fLoaded = false;
		m_spnData = {};
		m_pNTHeader32 = nullptr;
		m_pNTHeader64 = nullptr;
		m_stFileInfo = { };
		m_stMSDOSHeader = { };
		m_vecRichHeader.clear();
		m_stNTHeader = { };
		m_vecDataDirs.clear();
		m_vecSecHeaders.clear();
		m_stExport = { };
		m_vecImport.clear();
		m_stResource = { };
		m_vecException.clear();
		m_vecSecurity.clear();
		m_vecRelocs.clear();
		m_vecDebug.clear();
		m_stTLS = { };
		m_stLCD = { };
		m_vecBoundImp.clear();
		m_vecDelayImp.clear();
		m_stCOR20Desc = { };
		m_ptr.reset();
	}

	auto Clibpe::GetBaseAddr()const->DWORD_PTR {
		return reinterpret_cast<DWORD_PTR>(m_ptr.get());
	}

	auto Clibpe::GetDataSize()const->ULONGLONG {
		return m_spnData.size();
	}

	auto Clibpe::GetDosPtr()const->const IMAGE_DOS_HEADER* {
		return reinterpret_cast<const IMAGE_DOS_HEADER*>(m_spnData.data());
	}

	auto Clibpe::GetDirEntryRVA(DWORD dwEntry)const->DWORD {
		if (!m_stFileInfo.HasNTHdr)
			return { };

		if (m_stFileInfo.IsPE32)
			return m_pNTHeader32->OptionalHeader.DataDirectory[dwEntry].VirtualAddress;

		if (m_stFileInfo.IsPE64)
			return m_pNTHeader64->OptionalHeader.DataDirectory[dwEntry].VirtualAddress;

		return { };
	}

	auto Clibpe::GetDirEntrySize(DWORD dwEntry)const->DWORD {
		if (!m_stFileInfo.HasNTHdr)
			return { };

		if (m_stFileInfo.IsPE32)
			return m_pNTHeader32->OptionalHeader.DataDirectory[dwEntry].Size;

		if (m_stFileInfo.IsPE64)
			return m_pNTHeader64->OptionalHeader.DataDirectory[dwEntry].Size;

		return { };
	}

	auto Clibpe::GetImageBase()const->ULONGLONG {
		if (m_stFileInfo.IsPE32) {
			return m_pNTHeader32->OptionalHeader.ImageBase;
		}

		if (m_stFileInfo.IsPE64) {
			return m_pNTHeader64->OptionalHeader.ImageBase;
		}

		return { };
	}

	auto Clibpe::GetSecHdrFromName(LPCSTR lpszName)const->PIMAGE_SECTION_HEADER {
		PIMAGE_SECTION_HEADER pSecHdr;
		WORD wNumberOfSections;

		if (m_stFileInfo.IsPE32 && m_stFileInfo.HasNTHdr) {
			pSecHdr = IMAGE_FIRST_SECTION(m_pNTHeader32);
			wNumberOfSections = m_pNTHeader32->FileHeader.NumberOfSections;
		}
		else if (m_stFileInfo.IsPE64 && m_stFileInfo.HasNTHdr) {
			pSecHdr = IMAGE_FIRST_SECTION(m_pNTHeader64);
			wNumberOfSections = m_pNTHeader64->FileHeader.NumberOfSections;
		}
		else
			return nullptr;

		for (unsigned i = 0; i < wNumberOfSections; ++i, ++pSecHdr) {
			if (!IsPtrSafe(reinterpret_cast<DWORD_PTR>(pSecHdr) + sizeof(IMAGE_SECTION_HEADER)))
				break;
			if (strncmp(reinterpret_cast<char*>(pSecHdr->Name), lpszName, IMAGE_SIZEOF_SHORT_NAME) == 0)
				return pSecHdr;
		}

		return nullptr;
	}

	auto Clibpe::GetSecHdrFromRVA(ULONGLONG ullRVA)const->PIMAGE_SECTION_HEADER {
		PIMAGE_SECTION_HEADER pSecHdr;
		WORD wNumOfSections;

		if (m_stFileInfo.IsPE32 && m_stFileInfo.HasNTHdr) {
			pSecHdr = IMAGE_FIRST_SECTION(m_pNTHeader32);
			wNumOfSections = m_pNTHeader32->FileHeader.NumberOfSections;
		}
		else if (m_stFileInfo.IsPE64 && m_stFileInfo.HasNTHdr) {
			pSecHdr = IMAGE_FIRST_SECTION(m_pNTHeader64);
			wNumOfSections = m_pNTHeader64->FileHeader.NumberOfSections;
		}
		else
			return nullptr;

		for (unsigned i = 0; i < wNumOfSections; ++i, ++pSecHdr) {
			if (!IsPtrSafe(reinterpret_cast<DWORD_PTR>(pSecHdr) + sizeof(IMAGE_SECTION_HEADER)))
				return nullptr;
			//Is RVA within this section?
			if ((ullRVA >= pSecHdr->VirtualAddress) && (ullRVA < (pSecHdr->VirtualAddress + pSecHdr->Misc.VirtualSize)))
				return pSecHdr;
		}

		return nullptr;
	}

	template<typename T>
	auto Clibpe::GetTData(ULONGLONG ullOffset)const->T {
		if (ullOffset > (GetDataSize() - sizeof(T))) //Check for file size exceeding.
			return { };

		return *reinterpret_cast<T*>(GetBaseAddr() + ullOffset);
	}

	template<typename T>
	auto Clibpe::IsPtrSafe(const T tAddr, bool fCanReferenceBoundary)const->bool {
		/**************************************************************************************************
		* This func checks given pointer for nullptr and, more important, whether it fits allowed bounds. *
		* In PE headers there are plenty of places where wrong (bogus) values for pointers might reside,  *
		* causing many runtime «fun» if trying to dereference them.                                       *
		* Second arg (fCanReferenceBoundary) shows if pointer can point to the very end of a file, it's   *
		* valid for some PE structures. Template is used just for convenience, sometimes there is a need  *
		* to check pure address DWORD_PTR instead of a pointer.                                           *
		**************************************************************************************************/
		DWORD_PTR dwAddr;
		if constexpr (!std::is_same_v<T, DWORD_PTR>) {
			dwAddr = reinterpret_cast<DWORD_PTR>(tAddr);
		}
		else {
			dwAddr = tAddr;
		}

		const auto ullMaxAddr = GetBaseAddr() + GetDataSize();

		return dwAddr == 0 ? false : (fCanReferenceBoundary ?
			((dwAddr <= ullMaxAddr) && (dwAddr >= GetBaseAddr())) :
			((dwAddr < ullMaxAddr) && (dwAddr >= GetBaseAddr())));
	}

	auto Clibpe::PtrToOffset(LPCVOID lp)const->DWORD {
		if (lp == nullptr)
			return 0;

		return static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(lp) - GetBaseAddr());
	}

	auto Clibpe::RVAToOffset(ULONGLONG ullRVA)const->DWORD {
		DWORD dwOffset{ };
		for (const auto& iter : m_vecSecHeaders) {
			const auto& pSecHdr = iter.SecHdr;
			//Is RVA within this section?
			if ((ullRVA >= pSecHdr.VirtualAddress) && (ullRVA < (pSecHdr.VirtualAddress + pSecHdr.Misc.VirtualSize))) {
				dwOffset = static_cast<DWORD>(ullRVA) - (pSecHdr.VirtualAddress - pSecHdr.PointerToRawData);
				if (dwOffset > static_cast<DWORD>(GetDataSize()))
					dwOffset = 0;
			}
		}

		return dwOffset;
	}

	auto Clibpe::RVAToPtr(ULONGLONG ullRVA)const->LPVOID {
		const auto pSecHdr = GetSecHdrFromRVA(ullRVA);
		if (pSecHdr == nullptr)
			return nullptr;

		const auto ptr = reinterpret_cast<LPVOID>(GetBaseAddr() + ullRVA
			- static_cast<DWORD_PTR>(pSecHdr->VirtualAddress - pSecHdr->PointerToRawData));

		return IsPtrSafe(ptr, true) ? ptr : nullptr;
	}

	bool Clibpe::ParseMSDOSHeader() {
		const auto pDosHdr = GetDosPtr();

		//If file has at least MSDOS header signature then we can assume, 
		//that this is a minimally correct PE file and process further.
		if (pDosHdr->e_magic != IMAGE_DOS_SIGNATURE)
			return false;

		m_stMSDOSHeader = *pDosHdr;
		m_stFileInfo.HasDosHdr = true;

		return true;
	}

	bool Clibpe::ParseRichHeader() {
		//Undocumented, so called «Rich» header, dwells not in all PE files.
		//«Rich» stub starts at 0x80 offset, before pDosHdr->e_lfanew (PE header offset start).
		//If e_lfanew <= 0x80 — there is no «Rich» header.

		const auto ullBaseAddr = GetBaseAddr();
		const auto e_lfanew = GetDosPtr()->e_lfanew;
		if (e_lfanew <= 0x80 || !IsPtrSafe(ullBaseAddr + static_cast<DWORD_PTR>(e_lfanew)))
			return false;

		const auto pRichStartVA = reinterpret_cast<PDWORD>(ullBaseAddr + 0x80);
		PDWORD pRichIter = pRichStartVA;

		for (auto i = 0; i < ((e_lfanew - 0x80) / 4); ++i, ++pRichIter) {
			//Check "Rich" (ANSI) sign, it's always at the end of the «Rich» header.
			//Then take DWORD right after the "Rich" sign — it's a xor mask.
			//Apply this mask to the first DWORD of «Rich» header, it must be "DanS" (ANSI) after xoring.
			if ((*pRichIter == 0x68636952/*"Rich"*/) && ((*pRichStartVA ^ *(pRichIter + 1)) == 0x536E6144/*"Dans"*/)
				&& (reinterpret_cast<DWORD_PTR>(pRichIter) >= ullBaseAddr + 0x90)) { //To avoid too small (bogus) «Rich» header.
				//Amount of all «Rich» DOUBLE_DWORD structs.
				//First 16 bytes in «Rich» header are irrelevant. It's "DanS" itself and 12 more zeroed bytes.
				//That's why we subtracting 0x90 to find out amount of all «Rich» structures:
				//0x80 («Rich» start) + 16 (0x10) = 0x90.
				const DWORD dwRichSize = static_cast<DWORD>((reinterpret_cast<DWORD_PTR>(pRichIter) - ullBaseAddr) - 0x90) / 8;
				const DWORD dwRichXORMask = *(pRichIter + 1); //xor mask of «Rich» header.
				pRichIter = reinterpret_cast<PDWORD>(ullBaseAddr + 0x90);//VA of «Rich» DOUBLE_DWORD structs start.

				for (unsigned j = 0; j < dwRichSize; ++j) {
					//Pushing double DWORD of «Rich» structure.
					//Disassembling first DWORD by two WORDs.
					m_vecRichHeader.emplace_back(static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(pRichIter) - GetBaseAddr()),
						HIWORD(dwRichXORMask ^ *pRichIter), LOWORD(dwRichXORMask ^ *pRichIter), dwRichXORMask ^ *(pRichIter + 1));
					pRichIter += 2; //Jump to the next DOUBLE_DWORD.
				}

				m_stFileInfo.HasRichHdr = true;

				return true;
			}
		}

		return false;
	}

	bool Clibpe::ParseNTFileOptHeader() {
		const auto pNTHeader = reinterpret_cast<PIMAGE_NT_HEADERS32>(GetBaseAddr()
			+ static_cast<DWORD_PTR>(GetDosPtr()->e_lfanew));
		if (!IsPtrSafe(reinterpret_cast<DWORD_PTR>(pNTHeader) + sizeof(IMAGE_NT_HEADERS32)))
			return false;

		if (pNTHeader->Signature != IMAGE_NT_SIGNATURE)
			return false;

		switch (pNTHeader->OptionalHeader.Magic) {
			case IMAGE_NT_OPTIONAL_HDR32_MAGIC:
				m_stFileInfo.IsPE32 = true;
				m_pNTHeader32 = pNTHeader;
				m_stNTHeader.NTHdr32 = *m_pNTHeader32;
				m_stNTHeader.dwOffset = PtrToOffset(m_pNTHeader32);
				break;
			case IMAGE_NT_OPTIONAL_HDR64_MAGIC:
				m_stFileInfo.IsPE64 = true;
				m_pNTHeader64 = reinterpret_cast<PIMAGE_NT_HEADERS64>(pNTHeader);
				m_stNTHeader.NTHdr64 = *m_pNTHeader64;
				m_stNTHeader.dwOffset = PtrToOffset(m_pNTHeader64);
				break;
				//case IMAGE_ROM_OPTIONAL_HDR_MAGIC: //Not implemented yet.
			default:
				return false;
		}

		m_stFileInfo.HasNTHdr = true;

		return true;
	}

	bool Clibpe::ParseDataDirectories() {
		PIMAGE_DATA_DIRECTORY pDataDir;
		PIMAGE_SECTION_HEADER pSecHdr;
		DWORD dwRVAAndSizes;

		if (m_stFileInfo.IsPE32 && m_stFileInfo.HasNTHdr) {
			pDataDir = reinterpret_cast<PIMAGE_DATA_DIRECTORY>(m_pNTHeader32->OptionalHeader.DataDirectory);
			dwRVAAndSizes = m_pNTHeader32->OptionalHeader.NumberOfRvaAndSizes;
		}
		else if (m_stFileInfo.IsPE64 && m_stFileInfo.HasNTHdr) {
			pDataDir = reinterpret_cast<PIMAGE_DATA_DIRECTORY>(m_pNTHeader64->OptionalHeader.DataDirectory);
			dwRVAAndSizes = m_pNTHeader64->OptionalHeader.NumberOfRvaAndSizes;
		}
		else
			return false;

		//Filling DataDirectories vector.
		for (unsigned i = 0; i < (dwRVAAndSizes > 15 ? 15 : dwRVAAndSizes); ++i, ++pDataDir) {
			std::string strSecName;

			pSecHdr = GetSecHdrFromRVA(pDataDir->VirtualAddress);
			//RVA of IMAGE_DIRECTORY_ENTRY_SECURITY is the file RAW offset.
			if (pSecHdr && (i != IMAGE_DIRECTORY_ENTRY_SECURITY))
				strSecName.assign(reinterpret_cast<char* const>(pSecHdr->Name), 8);

			m_vecDataDirs.emplace_back(*pDataDir, std::move(strSecName));
		}

		if (m_vecDataDirs.empty())
			return false;

		m_stFileInfo.HasDataDirs = true;

		return true;
	}

	bool Clibpe::ParseSectionsHeaders() {
		PIMAGE_SECTION_HEADER pSecHdr;
		WORD wNumSections;
		DWORD dwSymbolTable, dwNumberOfSymbols;

		if (m_stFileInfo.IsPE32 && m_stFileInfo.HasNTHdr) {
			pSecHdr = IMAGE_FIRST_SECTION(m_pNTHeader32);
			wNumSections = m_pNTHeader32->FileHeader.NumberOfSections;
			dwSymbolTable = m_pNTHeader32->FileHeader.PointerToSymbolTable;
			dwNumberOfSymbols = m_pNTHeader32->FileHeader.NumberOfSymbols;
		}
		else if (m_stFileInfo.IsPE64 && m_stFileInfo.HasNTHdr) {
			pSecHdr = IMAGE_FIRST_SECTION(m_pNTHeader64);
			wNumSections = m_pNTHeader64->FileHeader.NumberOfSections;
			dwSymbolTable = m_pNTHeader64->FileHeader.PointerToSymbolTable;
			dwNumberOfSymbols = m_pNTHeader64->FileHeader.NumberOfSymbols;
		}
		else
			return false;

		m_vecSecHeaders.reserve(wNumSections);

		for (unsigned i = 0; i < wNumSections; ++i, ++pSecHdr) {
			if (!IsPtrSafe(reinterpret_cast<DWORD_PTR>(pSecHdr) + sizeof(IMAGE_SECTION_HEADER)))
				break;

			std::string strSecRealName{ };
			if (pSecHdr->Name[0] == '/') {	//Deprecated, but still used "feature" of section name.
				//https://docs.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_section_header
				//«An 8-byte, null-padded UTF-8 string. There is no terminating null character 
				//if the string is exactly eight characters long.
				//For longer names, this member contains a forward slash (/) followed by an ASCII representation 
				//of a decimal number that is an offset into the string table.»
				//String Table dwells right after the end of Symbol Table.
				//Each symbol in Symbol Table occupies exactly 18 bytes.
				//So String Table's beginning can be calculated like this:
				//FileHeader.PointerToSymbolTable + FileHeader.NumberOfSymbols * 18;

				const auto pStart = reinterpret_cast<const char*>(&pSecHdr->Name[1]);
				char* pEnd{ };
				errno = 0;
				const auto lOffset = strtol(pStart, &pEnd, 10);
				if (pEnd == pStart || errno == ERANGE)
					continue; //Going next section entry.

				const auto lpszSecRealName = reinterpret_cast<const char*>(GetBaseAddr()
					+ static_cast<DWORD_PTR>(dwSymbolTable) + static_cast<DWORD_PTR>(dwNumberOfSymbols) * 18
					+ static_cast<DWORD_PTR>(lOffset));
				if (IsPtrSafe(lpszSecRealName))
					strSecRealName = lpszSecRealName;
			}
			else {
				if (pSecHdr->Name[7] == 0)
					strSecRealName.assign((char const*)pSecHdr->Name);
				else
					strSecRealName.assign((char const*)pSecHdr->Name, 8);
			}

			m_vecSecHeaders.emplace_back(PtrToOffset(pSecHdr), *pSecHdr, std::move(strSecRealName));
		}

		if (m_vecSecHeaders.empty())
			return false;

		m_stFileInfo.HasSections = true;

		return true;
	}

	bool Clibpe::ParseExport() {
		const auto dwExportStartRVA = GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_EXPORT);
		const auto dwExportEndRVA = dwExportStartRVA + GetDirEntrySize(IMAGE_DIRECTORY_ENTRY_EXPORT);
		const auto pExportDir = static_cast<PIMAGE_EXPORT_DIRECTORY>(RVAToPtr(dwExportStartRVA));
		if (pExportDir == nullptr)
			return false;

		const auto pdwFuncsRVA = static_cast<PDWORD>(RVAToPtr(pExportDir->AddressOfFunctions));
		if (pdwFuncsRVA == nullptr)
			return false;

		const auto pwOrdinals = static_cast<PWORD>(RVAToPtr(pExportDir->AddressOfNameOrdinals));
		const auto pdwNamesRVA = static_cast<PDWORD>(RVAToPtr(pExportDir->AddressOfNames));
		std::vector<PEExportFunction> vecFuncs;
		std::string strModuleName;

		try {
			for (size_t iterFuncs = 0; iterFuncs < static_cast<size_t>(pExportDir->NumberOfFunctions); ++iterFuncs) {
				if (!IsPtrSafe(pdwFuncsRVA + iterFuncs)) //Checking pdwFuncsRVA array.
					break;

				if (pdwFuncsRVA[iterFuncs]) { //if RVA==0 —> going next entry.
					std::string strFuncName;
					std::string strForwarderName;
					DWORD dwNameRVA{ };
					if (pdwNamesRVA && pwOrdinals) {
						for (size_t iterFuncNames = 0; iterFuncNames < static_cast<size_t>(pExportDir->NumberOfNames); ++iterFuncNames) {
							if (!IsPtrSafe(pwOrdinals + iterFuncNames)) //Checking pwOrdinals array.
								break;

							if (pwOrdinals[iterFuncNames] == iterFuncs) { //Cycling through ordinals table to get func name.
								dwNameRVA = pdwNamesRVA[iterFuncNames];
								const auto pszFuncName = static_cast<LPCSTR>(RVAToPtr(dwNameRVA));
								//Checking func name for length correctness.
								if (pszFuncName && (StringCchLengthA(pszFuncName, MAX_PATH, nullptr) != STRSAFE_E_INVALID_PARAMETER)) {
									strFuncName = pszFuncName;
								}
								break;
							}
						}
					}

					if ((pdwFuncsRVA[iterFuncs] >= dwExportStartRVA) && (pdwFuncsRVA[iterFuncs] <= dwExportEndRVA)) {
						const auto pszForwarderName = static_cast<LPCSTR>(RVAToPtr(pdwFuncsRVA[iterFuncs]));
						//Checking forwarder name for length correctness.
						if (pszForwarderName && (StringCchLengthA(pszForwarderName, MAX_PATH, nullptr) != STRSAFE_E_INVALID_PARAMETER))
							strForwarderName = pszForwarderName;
					}
					vecFuncs.emplace_back(pdwFuncsRVA[iterFuncs], static_cast<DWORD>(iterFuncs)/*Ordinal*/, dwNameRVA,
						std::move(strFuncName), std::move(strForwarderName));
				}
			}
			const auto szExportName = static_cast<LPCSTR>(RVAToPtr(pExportDir->Name));
			//Checking Export name for length correctness.
			if (szExportName && (StringCchLengthA(szExportName, MAX_PATH, nullptr) != STRSAFE_E_INVALID_PARAMETER))
				strModuleName = szExportName;

			m_stExport = { PtrToOffset(pExportDir), *pExportDir, std::move(strModuleName) /*Actual IMG name*/, std::move(vecFuncs) };
		}
		catch (const std::bad_alloc&) {
			m_pEmergencyMemory.reset();
			MessageBoxW(nullptr, L"E_OUTOFMEMORY error while trying to get Export table.\nFile seems to be corrupted.",
				L"Error", MB_ICONERROR);

			vecFuncs.clear();
			m_pEmergencyMemory = std::make_unique<char[]>(0x8FFF);
		}
		catch (...) {
			MessageBoxW(nullptr, L"Unknown exception raised while trying to get Export table.\r\nFile seems to be corrupted.",
				L"Error", MB_ICONERROR);
		}

		m_stFileInfo.HasExport = true;

		return true;
	}

	bool Clibpe::ParseImport() {
		auto pImpDesc = static_cast<PIMAGE_IMPORT_DESCRIPTOR>(RVAToPtr(GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_IMPORT)));
		if (pImpDesc == nullptr)
			return false;

		try {
			//Counter for import modules. If it exceeds iMaxModules we stop parsing file, it's definitely bogus.
			//Very unlikely PE file has more than 1000 import modules.
			constexpr auto iMaxModules = 1000;
			constexpr auto iMaxFuncs = 5000;
			int iModulesCount = 0;

			if (m_stFileInfo.IsPE32) {
				while (pImpDesc->Name) {
					auto pThunk32 = reinterpret_cast<PIMAGE_THUNK_DATA32>(static_cast<DWORD_PTR>(pImpDesc->OriginalFirstThunk));
					if (!pThunk32)
						pThunk32 = reinterpret_cast<PIMAGE_THUNK_DATA32>(static_cast<DWORD_PTR>(pImpDesc->FirstThunk));

					if (pThunk32) {
						pThunk32 = static_cast<PIMAGE_THUNK_DATA32>(RVAToPtr(reinterpret_cast<DWORD_PTR>(pThunk32)));
						if (!pThunk32)
							break;

						std::vector<PEImportFunction> vecFunc{ };
						std::string strDllName{ };
						//Counter for import module funcs, if it exceeds iMaxFuncs we stop parsing import descr, it's definitely bogus.
						int iFuncsCount = 0;

						while (pThunk32->u1.AddressOfData) {
							PEImportFunction::UNPEIMPORTTHUNK unImpThunk32;
							unImpThunk32.Thunk32 = *pThunk32;
							IMAGE_IMPORT_BY_NAME stImpByName{ };
							std::string strFuncName{ };
							if (!(pThunk32->u1.Ordinal & IMAGE_ORDINAL_FLAG32)) {
								const auto pName = static_cast<PIMAGE_IMPORT_BY_NAME>(RVAToPtr(pThunk32->u1.AddressOfData));
								if (pName && (StringCchLengthA(pName->Name, MAX_PATH, nullptr) != STRSAFE_E_INVALID_PARAMETER)) {
									stImpByName = *pName;
									strFuncName = pName->Name;
								}
							}
							vecFunc.emplace_back(unImpThunk32, stImpByName, std::move(strFuncName));

							if (!IsPtrSafe(++pThunk32))
								break;
							if (++iFuncsCount == iMaxFuncs)
								break;
						}

						const auto szName = static_cast<LPCSTR>(RVAToPtr(pImpDesc->Name));
						if (szName && (StringCchLengthA(szName, MAX_PATH, nullptr) != STRSAFE_E_INVALID_PARAMETER))
							strDllName = szName;

						m_vecImport.emplace_back(PtrToOffset(pImpDesc), *pImpDesc, std::move(strDllName), std::move(vecFunc));

						if (!IsPtrSafe(++pImpDesc))
							break;
					}
					else //No IMPORT pointers for that DLL?...
						if (!IsPtrSafe(++pImpDesc))  //Going next dll.
							break;

					if (++iModulesCount == iMaxModules)
						break;
				}
			}
			else if (m_stFileInfo.IsPE64) {
				while (pImpDesc->Name) {
					auto pThunk64 = reinterpret_cast<PIMAGE_THUNK_DATA64>(static_cast<DWORD_PTR>(pImpDesc->OriginalFirstThunk));
					if (!pThunk64)
						pThunk64 = reinterpret_cast<PIMAGE_THUNK_DATA64>(static_cast<DWORD_PTR>(pImpDesc->FirstThunk));

					if (pThunk64) {
						pThunk64 = static_cast<PIMAGE_THUNK_DATA64>(RVAToPtr(reinterpret_cast<DWORD_PTR>(pThunk64)));
						if (!pThunk64)
							return false;

						std::vector<PEImportFunction> vecFunc{ };
						std::string strDllName{ };
						int iFuncsCount = 0;

						while (pThunk64->u1.AddressOfData) {
							PEImportFunction::UNPEIMPORTTHUNK unImpThunk64;
							unImpThunk64.Thunk64 = *pThunk64;
							IMAGE_IMPORT_BY_NAME stImpByName{ };
							std::string strFuncName{ };
							if (!(pThunk64->u1.Ordinal & IMAGE_ORDINAL_FLAG32)) {
								const auto pName = static_cast<PIMAGE_IMPORT_BY_NAME>(RVAToPtr(pThunk64->u1.AddressOfData));
								if (pName && (StringCchLengthA(pName->Name, MAX_PATH, nullptr) != STRSAFE_E_INVALID_PARAMETER)) {
									stImpByName = *pName;
									strFuncName = pName->Name;
								}
							}
							vecFunc.emplace_back(unImpThunk64, stImpByName, std::move(strFuncName));

							pThunk64++;
							if (++iFuncsCount == iMaxFuncs)
								break;
							if (++iFuncsCount == iMaxFuncs)
								break;
						}

						const auto szName = static_cast<LPCSTR>(RVAToPtr(pImpDesc->Name));
						if (szName && (StringCchLengthA(szName, MAX_PATH, nullptr) != STRSAFE_E_INVALID_PARAMETER))
							strDllName = szName;

						m_vecImport.emplace_back(PtrToOffset(pImpDesc), *pImpDesc, std::move(strDllName), std::move(vecFunc));

						if (!IsPtrSafe(++pImpDesc))
							break;
					}
					else
						if (!IsPtrSafe(++pImpDesc))
							break;

					if (++iModulesCount == iMaxModules)
						break;
				}
			}
		}
		catch (const std::bad_alloc&) {
			m_pEmergencyMemory.reset();
			MessageBoxW(nullptr, L"E_OUTOFMEMORY error while trying to get Import table.\r\n"
				L"Too many import entries!\nFile seems to be corrupted.", L"Error", MB_ICONERROR);

			m_vecImport.clear();
			m_pEmergencyMemory = std::make_unique<char[]>(0x8FFF);
		}
		catch (...) {
			MessageBoxW(nullptr, L"Unknown exception raised while trying to get Import table.\r\nFile seems to be corrupted.",
				L"Error", MB_ICONERROR);
		}

		m_stFileInfo.HasImport = true;

		return true;
	}

	bool Clibpe::ParseResources() {
		const auto pResDirRoot = static_cast<PIMAGE_RESOURCE_DIRECTORY>(RVAToPtr(GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_RESOURCE)));
		if (pResDirRoot == nullptr)
			return false;

		auto pResDirEntryRoot = reinterpret_cast<PIMAGE_RESOURCE_DIRECTORY_ENTRY>(pResDirRoot + 1);
		if (!IsPtrSafe(pResDirEntryRoot))
			return false;

		PIMAGE_RESOURCE_DIR_STRING_U pResDirStr;

		try {
			const DWORD dwNumOfEntriesRoot = pResDirRoot->NumberOfNamedEntries + pResDirRoot->NumberOfIdEntries;
			if (!IsPtrSafe(pResDirEntryRoot + dwNumOfEntriesRoot))
				return false;

			std::vector<PEResRootData> vecResDataRoot;
			vecResDataRoot.reserve(dwNumOfEntriesRoot);
			for (unsigned iLvLRoot = 0; iLvLRoot < dwNumOfEntriesRoot; ++iLvLRoot) {
				PIMAGE_RESOURCE_DATA_ENTRY pResDataEntryRoot{ };
				std::wstring wstrResNameRoot{ };
				std::vector<std::byte> vecRawResDataRoot{ };
				PEResLevel2 stResLvL2{ };

				//Name of Resource Type (ICON, BITMAP, MENU, etc...).
				if (pResDirEntryRoot->NameIsString) {
					if (IsSumOverflow(reinterpret_cast<DWORD_PTR>(pResDirRoot), static_cast<DWORD_PTR>(pResDirEntryRoot->NameOffset)))
						break;
					pResDirStr = reinterpret_cast<PIMAGE_RESOURCE_DIR_STRING_U>(reinterpret_cast<DWORD_PTR>(pResDirRoot)
						+ static_cast<DWORD_PTR>(pResDirEntryRoot->NameOffset));
					if (IsPtrSafe(pResDirStr))
						//Copy not more then MAX_PATH chars into wstrResNameRoot, avoiding overflow.
						wstrResNameRoot.assign(pResDirStr->NameString, pResDirStr->Length < MAX_PATH ? pResDirStr->Length : MAX_PATH);
				}
				if (pResDirEntryRoot->DataIsDirectory) {
					const auto pResDirLvL2 = reinterpret_cast<PIMAGE_RESOURCE_DIRECTORY>(reinterpret_cast<DWORD_PTR>(pResDirRoot)
						+ static_cast<DWORD_PTR>(pResDirEntryRoot->OffsetToDirectory));
					if (!IsPtrSafe(pResDirLvL2))
						break;

					std::vector<PEResLevel2Data> vecResDataLvL2;
					if (pResDirLvL2 == pResDirRoot) //Resource loop hack
						stResLvL2 = { PtrToOffset(pResDirLvL2), *pResDirLvL2, vecResDataLvL2 };
					else {
						auto pResDirEntryLvL2 = reinterpret_cast<PIMAGE_RESOURCE_DIRECTORY_ENTRY>(pResDirLvL2 + 1);
						const DWORD dwNumOfEntriesLvL2 = pResDirLvL2->NumberOfNamedEntries + pResDirLvL2->NumberOfIdEntries;
						if (!IsPtrSafe(pResDirEntryLvL2 + dwNumOfEntriesLvL2))
							break;

						vecResDataLvL2.reserve(dwNumOfEntriesLvL2);
						for (unsigned iLvL2 = 0; iLvL2 < dwNumOfEntriesLvL2; ++iLvL2) {
							PIMAGE_RESOURCE_DATA_ENTRY pResDataEntryLvL2{ };
							std::wstring wstrResNameLvL2{ };
							std::vector<std::byte> vecRawResDataLvL2{ };
							PEResLevel3 stResLvL3{ };

							//Name of resource itself if not presented by ID ("AFX_MY_SUPER_DIALOG"...).
							if (pResDirEntryLvL2->NameIsString) {
								if (IsSumOverflow(reinterpret_cast<DWORD_PTR>(pResDirRoot), static_cast<DWORD_PTR>(pResDirEntryLvL2->NameOffset)))
									break;
								pResDirStr = reinterpret_cast<PIMAGE_RESOURCE_DIR_STRING_U>(reinterpret_cast<DWORD_PTR>(pResDirRoot)
									+ static_cast<DWORD_PTR>(pResDirEntryLvL2->NameOffset));
								if (IsPtrSafe(pResDirStr))
									//Copy not more then MAX_PATH chars into wstrResNameLvL2, avoiding overflow.
									wstrResNameLvL2.assign(pResDirStr->NameString, pResDirStr->Length < MAX_PATH ? pResDirStr->Length : MAX_PATH);
							}

							if (pResDirEntryLvL2->DataIsDirectory) {
								const auto pResDirLvL3 = reinterpret_cast<PIMAGE_RESOURCE_DIRECTORY>(reinterpret_cast<DWORD_PTR>(pResDirRoot)
									+ static_cast<DWORD_PTR>(pResDirEntryLvL2->OffsetToDirectory));
								if (!IsPtrSafe(pResDirLvL3))
									break;

								std::vector<PEResLevel3Data> vecResDataLvL3;
								if (pResDirLvL3 == pResDirLvL2 || pResDirLvL3 == pResDirRoot)
									stResLvL3 = { PtrToOffset(pResDirLvL3), *pResDirLvL3, vecResDataLvL3 };
								else {
									auto pResDirEntryLvL3 = reinterpret_cast<PIMAGE_RESOURCE_DIRECTORY_ENTRY>(pResDirLvL3 + 1);
									const DWORD dwNumOfEntriesLvL3 = pResDirLvL3->NumberOfNamedEntries + pResDirLvL3->NumberOfIdEntries;
									if (!IsPtrSafe(pResDirEntryLvL3 + dwNumOfEntriesLvL3))
										break;

									vecResDataLvL3.reserve(dwNumOfEntriesLvL3);
									for (unsigned iLvL3 = 0; iLvL3 < dwNumOfEntriesLvL3; ++iLvL3) {
										std::wstring wstrResNameLvL3{ };
										std::vector<std::byte> vecRawResDataLvL3{ };

										if (pResDirEntryLvL3->NameIsString) {
											if (IsSumOverflow(reinterpret_cast<DWORD_PTR>(pResDirRoot), static_cast<DWORD_PTR>(pResDirEntryLvL3->NameOffset)))
												break;
											pResDirStr = reinterpret_cast<PIMAGE_RESOURCE_DIR_STRING_U>
												(reinterpret_cast<DWORD_PTR>(pResDirRoot) + static_cast<DWORD_PTR>(pResDirEntryLvL3->NameOffset));
											if (IsPtrSafe(pResDirStr))
												//Copy not more then MAX_PATH chars into wstrResNameLvL3, avoiding overflow.
												wstrResNameLvL3.assign(pResDirStr->NameString, pResDirStr->Length < MAX_PATH ? pResDirStr->Length : MAX_PATH);
										}

										const auto pResDataEntryLvL3 = reinterpret_cast<PIMAGE_RESOURCE_DATA_ENTRY>(reinterpret_cast<DWORD_PTR>(pResDirRoot)
											+ static_cast<DWORD_PTR>(pResDirEntryLvL3->OffsetToData));
										if (IsPtrSafe(pResDataEntryLvL3)) {	//Resource LvL 3 RAW Data.
											//IMAGE_RESOURCE_DATA_ENTRY::OffsetToData is actually a general RVA,
											//not an offset from root IMAGE_RESOURCE_DIRECTORY, like IMAGE_RESOURCE_DIRECTORY_ENTRY::OffsetToData.

											const auto pThirdResRawDataBegin = static_cast<std::byte*>(RVAToPtr(pResDataEntryLvL3->OffsetToData));
											//Checking RAW Resource data pointer out of bounds.
											if (pThirdResRawDataBegin && IsPtrSafe(reinterpret_cast<DWORD_PTR>(pThirdResRawDataBegin)
												+ static_cast<DWORD_PTR>(pResDataEntryLvL3->Size), true)) {
												vecRawResDataLvL3.assign(pThirdResRawDataBegin, pThirdResRawDataBegin + pResDataEntryLvL3->Size);
											}
										}

										vecResDataLvL3.emplace_back(*pResDirEntryLvL3, std::move(wstrResNameLvL3),
											IsPtrSafe(pResDataEntryLvL3) ? *pResDataEntryLvL3 : IMAGE_RESOURCE_DATA_ENTRY{ }, std::move(vecRawResDataLvL3));

										if (!IsPtrSafe(++pResDirEntryLvL3))
											break;
									}
									stResLvL3 = { PtrToOffset(pResDirLvL3), *pResDirLvL3, std::move(vecResDataLvL3) };
								}
							}
							else {	//////Resource LvL2 RAW Data.
								pResDataEntryLvL2 = reinterpret_cast<PIMAGE_RESOURCE_DATA_ENTRY>(reinterpret_cast<DWORD_PTR>(pResDirRoot)
									+ static_cast<DWORD_PTR>(pResDirEntryLvL2->OffsetToData));
								if (IsPtrSafe(pResDataEntryLvL2)) {
									const auto pSecondResRawDataBegin = static_cast<std::byte*>(RVAToPtr(pResDataEntryLvL2->OffsetToData));
									//Checking RAW Resource data pointer out of bounds.
									if (pSecondResRawDataBegin && IsPtrSafe(reinterpret_cast<DWORD_PTR>(pSecondResRawDataBegin)
										+ static_cast<DWORD_PTR>(pResDataEntryLvL2->Size), true)) {
										vecRawResDataLvL2.assign(pSecondResRawDataBegin, pSecondResRawDataBegin + pResDataEntryLvL2->Size);
									}
								}
							}
							vecResDataLvL2.emplace_back(*pResDirEntryLvL2, std::move(wstrResNameLvL2),
								IsPtrSafe(pResDataEntryLvL2) ? *pResDataEntryLvL2 : IMAGE_RESOURCE_DATA_ENTRY{ }, std::move(vecRawResDataLvL2), stResLvL3);

							if (!IsPtrSafe(++pResDirEntryLvL2))
								break;
						}
						stResLvL2 = { PtrToOffset(pResDirLvL2), *pResDirLvL2, std::move(vecResDataLvL2) };
					}
				}
				else {	//////Resource LvL Root RAW Data.
					pResDataEntryRoot = reinterpret_cast<PIMAGE_RESOURCE_DATA_ENTRY>(reinterpret_cast<DWORD_PTR>(pResDirRoot)
						+ static_cast<DWORD_PTR>(pResDirEntryRoot->OffsetToData));
					if (IsPtrSafe(pResDataEntryRoot)) {
						auto pRootResRawDataBegin = static_cast<std::byte*>(RVAToPtr(pResDataEntryRoot->OffsetToData));
						//Checking RAW Resource data pointer out of bounds.
						if (pRootResRawDataBegin && IsPtrSafe(reinterpret_cast<DWORD_PTR>(pRootResRawDataBegin)
							+ static_cast<DWORD_PTR>(pResDataEntryRoot->Size), true)) {
							vecRawResDataRoot.assign(pRootResRawDataBegin, pRootResRawDataBegin + pResDataEntryRoot->Size);
						}
					}
				}
				vecResDataRoot.emplace_back(*pResDirEntryRoot, std::move(wstrResNameRoot),
					IsPtrSafe(pResDataEntryRoot) ? *pResDataEntryRoot : IMAGE_RESOURCE_DATA_ENTRY{ }, std::move(vecRawResDataRoot), stResLvL2);

				if (!IsPtrSafe(++pResDirEntryRoot))
					break;
			}
			m_stResource = { PtrToOffset(pResDirRoot), *pResDirRoot, std::move(vecResDataRoot) };
		}
		catch (const std::bad_alloc&) {
			m_pEmergencyMemory.reset();
			MessageBoxW(nullptr, L"E_OUTOFMEMORY error while trying to get Resource table.\nFile seems to be corrupted.",
				L"Error", MB_ICONERROR);

			m_pEmergencyMemory = std::make_unique<char[]>(0x8FFF);
		}
		catch (...) {
			MessageBoxW(nullptr, L"Unknown exception raised while trying to get Resource table.\r\n\nFile seems to be corrupted.",
				L"Error", MB_ICONERROR);
		}

		m_stFileInfo.HasResource = true;

		return true;
	}

	bool Clibpe::ParseExceptions() {
		//IMAGE_RUNTIME_FUNCTION_ENTRY (without leading underscore)
		//might have different typedef depending on defined platform, see winnt.h
		auto pRuntimeFuncsEntry = static_cast<_PIMAGE_RUNTIME_FUNCTION_ENTRY>(RVAToPtr(GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_EXCEPTION)));
		if (pRuntimeFuncsEntry == nullptr)
			return false;

		const auto dwEntries = GetDirEntrySize(IMAGE_DIRECTORY_ENTRY_EXCEPTION) / static_cast<DWORD>(sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY));
		if (!dwEntries || !IsPtrSafe(reinterpret_cast<DWORD_PTR>(pRuntimeFuncsEntry) + static_cast<DWORD_PTR>(dwEntries)))
			return false;

		for (unsigned i = 0; i < dwEntries; ++i, ++pRuntimeFuncsEntry) {
			if (!IsPtrSafe(pRuntimeFuncsEntry))
				break;

			m_vecException.emplace_back(PtrToOffset(pRuntimeFuncsEntry), *pRuntimeFuncsEntry);
		}

		m_stFileInfo.HasException = true;

		return true;
	}

	bool Clibpe::ParseSecurity() {
		const auto dwSecurityDirOffset = GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_SECURITY);
		const auto dwSecurityDirSize = GetDirEntrySize(IMAGE_DIRECTORY_ENTRY_SECURITY);
		if (!dwSecurityDirOffset || !dwSecurityDirSize)
			return false;

		//Checks for bogus file offsets that can cause DWORD_PTR overflow.
		if (IsSumOverflow(static_cast<DWORD_PTR>(dwSecurityDirOffset), GetBaseAddr()))
			return false;

		auto dwSecurityDirStartVA = GetBaseAddr() + static_cast<DWORD_PTR>(dwSecurityDirOffset);
		if (IsSumOverflow(dwSecurityDirStartVA, static_cast<DWORD_PTR>(dwSecurityDirSize)))
			return false;

		const DWORD_PTR dwSecurityDirEndVA = dwSecurityDirStartVA + static_cast<DWORD_PTR>(dwSecurityDirSize);

		if (!IsPtrSafe(dwSecurityDirStartVA) || !IsPtrSafe(dwSecurityDirEndVA, true))
			return false;

		while (dwSecurityDirStartVA < dwSecurityDirEndVA) {
			auto pCertificate = reinterpret_cast<LPWIN_CERTIFICATE>(dwSecurityDirStartVA);
			const auto dwCertSize = pCertificate->dwLength - static_cast<DWORD>(offsetof(WIN_CERTIFICATE, bCertificate));
			if (!IsPtrSafe(dwSecurityDirStartVA + static_cast<DWORD_PTR>(dwCertSize)))
				break;

			m_vecSecurity.emplace_back(PtrToOffset(pCertificate), *pCertificate);

			//Get next certificate entry, all entries start at 8 aligned address.
			DWORD dwLength = pCertificate->dwLength;
			dwLength += (8 - (dwLength & 7)) & 7;
			dwSecurityDirStartVA = dwSecurityDirStartVA + static_cast<DWORD_PTR>(dwLength);
			if (!IsPtrSafe(dwSecurityDirStartVA))
				break;
		}

		m_stFileInfo.HasSecurity = true;

		return true;
	}

	bool Clibpe::ParseRelocations() {
		auto pBaseRelocDesc = static_cast<PIMAGE_BASE_RELOCATION>(RVAToPtr(GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_BASERELOC)));
		if (pBaseRelocDesc == nullptr)
			return false;

		try {
			if (!pBaseRelocDesc->SizeOfBlock || !pBaseRelocDesc->VirtualAddress)
				m_vecRelocs.emplace_back(PtrToOffset(pBaseRelocDesc), *pBaseRelocDesc, std::vector<PERelocData> { });

			while ((pBaseRelocDesc->SizeOfBlock) && (pBaseRelocDesc->VirtualAddress)) {
				if (pBaseRelocDesc->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION)) {
					m_vecRelocs.emplace_back(PtrToOffset(pBaseRelocDesc), *pBaseRelocDesc, std::vector<PERelocData>{ });
					break;
				}

				//Amount of Reloc entries.
				DWORD dwNumRelocEntries = (pBaseRelocDesc->SizeOfBlock - static_cast<DWORD>(sizeof(IMAGE_BASE_RELOCATION))) / static_cast<DWORD>(sizeof(WORD));
				auto pwRelocEntry = reinterpret_cast<PWORD>(reinterpret_cast<DWORD_PTR>(pBaseRelocDesc) + sizeof(IMAGE_BASE_RELOCATION));
				std::vector<PERelocData> vecRelocs;
				for (DWORD i = 0; i < dwNumRelocEntries; ++i, ++pwRelocEntry) {
					if (!IsPtrSafe(pwRelocEntry))
						break;
					//Getting HIGH 4 bits of reloc's entry WORD —> reloc type.
					WORD wRelocType = (*pwRelocEntry & 0xF000) >> 12;
					vecRelocs.emplace_back(PtrToOffset(pwRelocEntry), wRelocType, static_cast<WORD>((*pwRelocEntry) & 0x0fff)/*Low 12 bits —> Offset*/);
					if (wRelocType == IMAGE_REL_BASED_HIGHADJ) {	//The base relocation adds the high 16 bits of the difference to the 16-bit field at offset.
						//The 16-bit field represents the high value of a 32-bit word. 
						//The low 16 bits of the 32-bit value are stored in the 16-bit word that follows this base relocation.
						//This means that this base relocation occupies two slots. (MSDN)
						if (!IsPtrSafe(++pwRelocEntry)) {
							vecRelocs.clear();
							break;
						}

						vecRelocs.emplace_back(PtrToOffset(pwRelocEntry), wRelocType, *pwRelocEntry /*The low 16-bit field.*/);
						dwNumRelocEntries--; //to compensate pwRelocEntry++.
					}
				}

				m_vecRelocs.emplace_back(PtrToOffset(pBaseRelocDesc), *pBaseRelocDesc, std::move(vecRelocs));

				//Too big (bogus) SizeOfBlock may cause DWORD_PTR overflow. Checking to prevent.
				if (IsSumOverflow(reinterpret_cast<DWORD_PTR>(pBaseRelocDesc), static_cast<DWORD_PTR>(pBaseRelocDesc->SizeOfBlock)))
					break;

				pBaseRelocDesc = reinterpret_cast<PIMAGE_BASE_RELOCATION>(reinterpret_cast<DWORD_PTR>(pBaseRelocDesc)
					+ static_cast<DWORD_PTR>(pBaseRelocDesc->SizeOfBlock));
				if (!IsPtrSafe(pBaseRelocDesc))
					break;
			}
		}
		catch (const std::bad_alloc&) {
			m_pEmergencyMemory.reset();
			MessageBoxW(nullptr, L"E_OUTOFMEMORY error while trying to get Relocation table.\nFile seems to be corrupted.",
				L"Error", MB_ICONERROR);

			m_pEmergencyMemory = std::make_unique<char[]>(0x8FFF);
		}
		catch (...) {
			MessageBoxW(nullptr, L"Unknown exception raised while trying to get Relocation table.\nFile seems to be corrupted.",
				L"Error", MB_ICONERROR);
		}

		m_stFileInfo.HasReloc = true;

		return true;
	}

	bool Clibpe::ParseDebug() {
		const auto dwDebugDirRVA = GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_DEBUG);
		if (!dwDebugDirRVA)
			return false;

		PIMAGE_DEBUG_DIRECTORY pDebugDir;
		DWORD dwDebugDirSize;
		PIMAGE_SECTION_HEADER pDebugSecHdr = GetSecHdrFromName(".debug");
		if (pDebugSecHdr && (pDebugSecHdr->VirtualAddress == dwDebugDirRVA)) {
			pDebugDir = reinterpret_cast<PIMAGE_DEBUG_DIRECTORY>(GetBaseAddr() + static_cast<DWORD_PTR>(pDebugSecHdr->PointerToRawData));
			dwDebugDirSize = GetDirEntrySize(IMAGE_DIRECTORY_ENTRY_DEBUG) * static_cast<DWORD>(sizeof(IMAGE_DEBUG_DIRECTORY));
		}
		else //Looking for the debug directory.
		{
			if (pDebugSecHdr = GetSecHdrFromRVA(dwDebugDirRVA); pDebugSecHdr == nullptr)
				return false;

			if (pDebugDir = static_cast<PIMAGE_DEBUG_DIRECTORY>(RVAToPtr(dwDebugDirRVA)); pDebugDir == nullptr)
				return false;

			dwDebugDirSize = GetDirEntrySize(IMAGE_DIRECTORY_ENTRY_DEBUG);
		}

		const DWORD dwDebugEntries = dwDebugDirSize / static_cast<DWORD>(sizeof(IMAGE_DEBUG_DIRECTORY));

		if (!dwDebugEntries || IsSumOverflow(reinterpret_cast<DWORD_PTR>(pDebugDir), static_cast<DWORD_PTR>(dwDebugDirSize)) ||
			!IsPtrSafe(reinterpret_cast<DWORD_PTR>(pDebugDir) + static_cast<DWORD_PTR>(dwDebugDirSize)))
			return false;

		try {
			for (unsigned i = 0; i < dwDebugEntries; ++i) {
				PEDebugHeader stDbgHdr;
				for (unsigned iterDbgHdr = 0; iterDbgHdr < (sizeof(PEDebugHeader::Header) / sizeof(DWORD)); iterDbgHdr++) {
					stDbgHdr.Header[iterDbgHdr] = GetTData<DWORD>(static_cast<size_t>(pDebugDir->PointerToRawData) + (sizeof(DWORD) * iterDbgHdr));
				}

				if (pDebugDir->Type == IMAGE_DEBUG_TYPE_CODEVIEW) {
					DWORD dwOffset = 0;
					if (stDbgHdr.Header[0] == 0x53445352) //"RSDS"
						dwOffset = sizeof(DWORD) * 6;
					else if (stDbgHdr.Header[0] == 0x3031424E) //"NB10"
						dwOffset = sizeof(DWORD) * 4;

					std::string strPDBName;
					if (dwOffset > 0)
						for (unsigned iterStr = 0; iterStr < MAX_PATH; iterStr++) {
							const auto byte = GetTData<BYTE>(pDebugDir->PointerToRawData + dwOffset + iterStr);
							if (byte == 0) //End of string.
								break;
							strPDBName += byte;
						}
					stDbgHdr.PDBName = std::move(strPDBName);
				}

				m_vecDebug.emplace_back(PtrToOffset(pDebugDir), *pDebugDir, stDbgHdr);
				if (!IsPtrSafe(++pDebugDir))
					break;
			}

			m_stFileInfo.HasDebug = true;
		}
		catch (const std::bad_alloc&) {
			m_pEmergencyMemory.reset();
			MessageBoxW(nullptr, L"E_OUTOFMEMORY error while trying to get Debug info.\r\n"
				L"File seems to be corrupted.", L"Error", MB_ICONERROR);

			m_pEmergencyMemory = std::make_unique<char[]>(0x8FFF);
		}

		return true;
	}

	bool Clibpe::ParseArchitecture() {
		const auto dwArchDirRVA = GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_ARCHITECTURE);
		if (!dwArchDirRVA)
			return false;

		const auto pArchEntry = static_cast<PIMAGE_ARCHITECTURE_ENTRY>(RVAToPtr(dwArchDirRVA));
		if (!pArchEntry)
			return false;

		m_stFileInfo.HasArchitect = true;

		return true;
	}

	bool Clibpe::ParseGlobalPtr() {
		const auto dwGlobalPTRDirRVA = reinterpret_cast<DWORD_PTR>(RVAToPtr(GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_GLOBALPTR)));
		if (!dwGlobalPTRDirRVA)
			return false;

		m_stFileInfo.HasGlobalPtr = true;

		return true;
	}

	bool Clibpe::ParseTLS() {
		const auto dwTLSDirRVA = GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_TLS);
		if (!dwTLSDirRVA)
			return false;

		try {
			std::vector<DWORD> vecTLSCallbacks;
			ULONGLONG ullStartAddressOfRawData{ };
			ULONGLONG ullEndAddressOfRawData{ };
			ULONGLONG ullAddressOfCallBacks{ };
			PETLS::Tls varTLSDir;
			PDWORD pdwTLSPtr;

			if (m_stFileInfo.IsPE32) {
				const auto pTLSDir32 = static_cast<PIMAGE_TLS_DIRECTORY32>(RVAToPtr(dwTLSDirRVA));
				if (!pTLSDir32)
					return false;

				varTLSDir.TLSDir32 = *pTLSDir32;
				pdwTLSPtr = reinterpret_cast<PDWORD>(pTLSDir32);
				ullStartAddressOfRawData = pTLSDir32->StartAddressOfRawData;
				ullEndAddressOfRawData = pTLSDir32->EndAddressOfRawData;
				ullAddressOfCallBacks = pTLSDir32->AddressOfCallBacks;
			}
			else if (m_stFileInfo.IsPE64) {
				const auto pTLSDir64 = static_cast<PIMAGE_TLS_DIRECTORY64>(RVAToPtr(dwTLSDirRVA));
				if (!pTLSDir64)
					return false;

				varTLSDir.TLSDir64 = *pTLSDir64;
				pdwTLSPtr = reinterpret_cast<PDWORD>(pTLSDir64);
				ullStartAddressOfRawData = pTLSDir64->StartAddressOfRawData;
				ullEndAddressOfRawData = pTLSDir64->EndAddressOfRawData;
				ullAddressOfCallBacks = pTLSDir64->AddressOfCallBacks;
			}
			else
				return false;

			auto pTLSCallbacks = static_cast<PDWORD>(RVAToPtr(ullAddressOfCallBacks - GetImageBase()));
			if (pTLSCallbacks) {
				while (*pTLSCallbacks) {
					vecTLSCallbacks.push_back(*pTLSCallbacks);
					if (!IsPtrSafe(++pTLSCallbacks)) {
						vecTLSCallbacks.clear();
						break;
					}
				}
			}

			m_stTLS = PETLS{ PtrToOffset(pdwTLSPtr), varTLSDir, std::move(vecTLSCallbacks) };
			m_stFileInfo.HasTLS = true;
		}
		catch (const std::bad_alloc&) {
			m_pEmergencyMemory.reset();
			MessageBoxW(nullptr, L"E_OUTOFMEMORY error while trying to get TLS table.\r\n"
				L"File seems to be corrupted.", L"Error", MB_ICONERROR);

			m_pEmergencyMemory = std::make_unique<char[]>(0x8FFF);
		}
		catch (...) {
			MessageBoxW(nullptr, L"Unknown exception raised while trying to get TLS table.\r\nFile seems to be corrupted.",
				L"Error", MB_ICONERROR);
		}

		return true;
	}

	bool Clibpe::ParseLCD() {
		if (m_stFileInfo.IsPE32) {
			const auto pLCD32 = static_cast<PIMAGE_LOAD_CONFIG_DIRECTORY32>(RVAToPtr(GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG)));
			if (!pLCD32 || !IsPtrSafe(reinterpret_cast<DWORD_PTR>(pLCD32) + sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32)))
				return false;

			m_stLCD.dwOffset = PtrToOffset(pLCD32);
			m_stLCD.LCD32 = *pLCD32;
		}
		else if (m_stFileInfo.IsPE64) {
			const auto pLCD64 = static_cast<PIMAGE_LOAD_CONFIG_DIRECTORY64>(RVAToPtr(GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG)));
			if (!pLCD64 || !IsPtrSafe(reinterpret_cast<DWORD_PTR>(pLCD64) + sizeof(PIMAGE_LOAD_CONFIG_DIRECTORY64)))
				return false;

			m_stLCD.dwOffset = PtrToOffset(pLCD64);
			m_stLCD.LCD64 = *pLCD64;
		}
		else
			return false;

		m_stFileInfo.HasLoadCFG = true;

		return true;
	}

	bool Clibpe::ParseBoundImport() {
		auto pBoundImpDesc = static_cast<PIMAGE_BOUND_IMPORT_DESCRIPTOR>(RVAToPtr(GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT)));
		if (pBoundImpDesc == nullptr)
			return false;

		while (pBoundImpDesc->TimeDateStamp) {
			std::string strModuleName;
			std::vector<PEBoundForwarder> vecBoundForwarders;

			auto pBoundImpForwarder = reinterpret_cast<PIMAGE_BOUND_FORWARDER_REF>(pBoundImpDesc + 1);
			if (!IsPtrSafe(pBoundImpForwarder))
				break;

			for (unsigned i = 0; i < pBoundImpDesc->NumberOfModuleForwarderRefs; ++i) {
				std::string strForwarderModuleName{ };

				const auto szName = reinterpret_cast<LPCSTR>(reinterpret_cast<DWORD_PTR>(pBoundImpDesc) + pBoundImpForwarder->OffsetModuleName);
				if (IsPtrSafe(szName))
					if (szName && (StringCchLengthA(szName, MAX_PATH, nullptr) != STRSAFE_E_INVALID_PARAMETER))
						strForwarderModuleName = szName;

				vecBoundForwarders.emplace_back(PtrToOffset(pBoundImpForwarder), *pBoundImpForwarder, std::move(strForwarderModuleName));

				if (!IsPtrSafe(++pBoundImpForwarder))
					break;

				pBoundImpDesc = reinterpret_cast<PIMAGE_BOUND_IMPORT_DESCRIPTOR>(reinterpret_cast<DWORD_PTR>(pBoundImpDesc) + sizeof(IMAGE_BOUND_FORWARDER_REF));
				if (!IsPtrSafe(pBoundImpDesc))
					break;
			}

			const auto szName = reinterpret_cast<LPCSTR>(reinterpret_cast<DWORD_PTR>(pBoundImpDesc) + pBoundImpDesc->OffsetModuleName);
			if (IsPtrSafe(szName))
				if (StringCchLengthA(szName, MAX_PATH, nullptr) != STRSAFE_E_INVALID_PARAMETER)
					strModuleName = szName;

			m_vecBoundImp.emplace_back(PtrToOffset(pBoundImpDesc), *pBoundImpDesc, std::move(strModuleName), std::move(vecBoundForwarders));

			if (!IsPtrSafe(++pBoundImpDesc))
				break;
		}

		m_stFileInfo.HasBoundImp = true;

		return true;
	}

	bool Clibpe::ParseIAT() {
		const auto dwIATDirRVA = reinterpret_cast<DWORD_PTR>(RVAToPtr(GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_IAT)));
		if (!dwIATDirRVA)
			return false;

		m_stFileInfo.HasIAT = true;

		return true;
	}

	bool Clibpe::ParseDelayImport() {
		auto pDelayImpDescr = static_cast<PIMAGE_DELAYLOAD_DESCRIPTOR>(RVAToPtr(GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT)));
		if (pDelayImpDescr == nullptr)
			return false;

		if (m_stFileInfo.IsPE32) {
			while (pDelayImpDescr->DllNameRVA) {
				auto pThunk32Name = reinterpret_cast<PIMAGE_THUNK_DATA32>(static_cast<DWORD_PTR>(pDelayImpDescr->ImportNameTableRVA));

				if (!pThunk32Name) {
					if (!IsPtrSafe(++pDelayImpDescr))
						break;
				}
				else {
					std::string strDllName;
					std::vector<PEDelayImportFunc> vecFunc;

					pThunk32Name = static_cast<PIMAGE_THUNK_DATA32>(RVAToPtr(reinterpret_cast<DWORD_PTR>(pThunk32Name)));
					auto pThunk32IAT = static_cast<PIMAGE_THUNK_DATA32>(RVAToPtr(pDelayImpDescr->ImportAddressTableRVA));
					auto pThunk32BoundIAT = static_cast<PIMAGE_THUNK_DATA32>(RVAToPtr(pDelayImpDescr->BoundImportAddressTableRVA));
					auto pThunk32UnloadInfoTable = static_cast<PIMAGE_THUNK_DATA32>(RVAToPtr(pDelayImpDescr->UnloadInformationTableRVA));

					if (!pThunk32Name)
						break;

					while (pThunk32Name->u1.AddressOfData) {
						PEDelayImportFunc::PEDelayImportThunk unDelayImpThunk32{ };
						unDelayImpThunk32.st32.ImportAddressTable = *pThunk32Name;
						unDelayImpThunk32.st32.ImportNameTable = pThunk32IAT ? *pThunk32IAT : IMAGE_THUNK_DATA32{ };
						unDelayImpThunk32.st32.BoundImportAddressTable = pThunk32BoundIAT ? *pThunk32BoundIAT : IMAGE_THUNK_DATA32{ };
						unDelayImpThunk32.st32.UnloadInformationTable = pThunk32UnloadInfoTable ? *pThunk32UnloadInfoTable : IMAGE_THUNK_DATA32{ };

						std::string strFuncName{ };
						IMAGE_IMPORT_BY_NAME stImpByName{ };
						if (!(pThunk32Name->u1.Ordinal & IMAGE_ORDINAL_FLAG32)) {
							const auto pName = static_cast<PIMAGE_IMPORT_BY_NAME>(RVAToPtr(pThunk32Name->u1.AddressOfData));
							if (pName && (StringCchLengthA(pName->Name, MAX_PATH, nullptr) != STRSAFE_E_INVALID_PARAMETER)) {
								stImpByName = *pName;
								strFuncName = pName->Name;
							}
						}
						vecFunc.emplace_back(unDelayImpThunk32, stImpByName, std::move(strFuncName));

						if (!IsPtrSafe(++pThunk32Name))
							break;
						if (pThunk32IAT)
							if (!IsPtrSafe(++pThunk32IAT))
								break;
						if (pThunk32BoundIAT)
							if (!IsPtrSafe(++pThunk32BoundIAT))
								break;
						if (pThunk32UnloadInfoTable)
							if (!IsPtrSafe(++pThunk32UnloadInfoTable))
								break;
					}

					const auto szName = static_cast<LPCSTR>(RVAToPtr(pDelayImpDescr->DllNameRVA));
					if (szName && (StringCchLengthA(szName, MAX_PATH, nullptr) != STRSAFE_E_INVALID_PARAMETER))
						strDllName = szName;

					m_vecDelayImp.emplace_back(PtrToOffset(pDelayImpDescr), *pDelayImpDescr, std::move(strDllName), std::move(vecFunc));

					if (!IsPtrSafe(++pDelayImpDescr))
						break;
				}
			}
		}
		else if (m_stFileInfo.IsPE64) {
			while (pDelayImpDescr->DllNameRVA) {
				auto pThunk64Name = reinterpret_cast<PIMAGE_THUNK_DATA64>(static_cast<DWORD_PTR>(pDelayImpDescr->ImportNameTableRVA));

				if (!pThunk64Name) {
					if (!IsPtrSafe(++pDelayImpDescr))
						break;
				}
				else {
					std::string strDllName;
					std::vector<PEDelayImportFunc> vecFunc;

					pThunk64Name = static_cast<PIMAGE_THUNK_DATA64>(RVAToPtr(reinterpret_cast<DWORD_PTR>(pThunk64Name)));
					auto pThunk64IAT = static_cast<PIMAGE_THUNK_DATA64>(RVAToPtr(pDelayImpDescr->ImportAddressTableRVA));
					auto pThunk64BoundIAT = static_cast<PIMAGE_THUNK_DATA64>(RVAToPtr(pDelayImpDescr->BoundImportAddressTableRVA));
					auto pThunk64UnloadInfoTable = static_cast<PIMAGE_THUNK_DATA64>(RVAToPtr(pDelayImpDescr->UnloadInformationTableRVA));

					if (!pThunk64Name)
						break;

					while (pThunk64Name->u1.AddressOfData) {
						PEDelayImportFunc::PEDelayImportThunk unDelayImpThunk64{ };
						unDelayImpThunk64.st64.ImportAddressTable = *pThunk64Name;
						unDelayImpThunk64.st64.ImportNameTable = pThunk64IAT ? *pThunk64IAT : IMAGE_THUNK_DATA64{ };
						unDelayImpThunk64.st64.BoundImportAddressTable = pThunk64BoundIAT ? *pThunk64BoundIAT : IMAGE_THUNK_DATA64{ };
						unDelayImpThunk64.st64.UnloadInformationTable = pThunk64UnloadInfoTable ? *pThunk64UnloadInfoTable : IMAGE_THUNK_DATA64{ };

						std::string strFuncName{ };
						IMAGE_IMPORT_BY_NAME stImpByName{ };
						if (!(pThunk64Name->u1.Ordinal & IMAGE_ORDINAL_FLAG64)) {
							const auto pName = static_cast<PIMAGE_IMPORT_BY_NAME>(RVAToPtr(pThunk64Name->u1.AddressOfData));
							if (pName && (StringCchLengthA(pName->Name, MAX_PATH, nullptr) != STRSAFE_E_INVALID_PARAMETER)) {
								stImpByName = *pName;
								strFuncName = pName->Name;
							}
						}
						vecFunc.emplace_back(unDelayImpThunk64, stImpByName, std::move(strFuncName));

						if (!IsPtrSafe(++pThunk64Name))
							break;
						if (pThunk64IAT)
							if (!IsPtrSafe(++pThunk64IAT))
								break;
						if (pThunk64BoundIAT)
							if (!IsPtrSafe(++pThunk64BoundIAT))
								break;
						if (pThunk64UnloadInfoTable)
							if (!IsPtrSafe(++pThunk64UnloadInfoTable))
								break;
					}

					const auto szName = static_cast<LPCSTR>(RVAToPtr(pDelayImpDescr->DllNameRVA));
					if (szName && (StringCchLengthA(szName, MAX_PATH, nullptr) != STRSAFE_E_INVALID_PARAMETER))
						strDllName = szName;

					m_vecDelayImp.emplace_back(PtrToOffset(pDelayImpDescr), *pDelayImpDescr, std::move(strDllName), std::move(vecFunc));

					if (!IsPtrSafe(++pDelayImpDescr))
						break;
				}
			}
		}

		m_stFileInfo.HasDelayImp = true;

		return true;
	}

	bool Clibpe::ParseCOMDescriptor() {
		const auto pCOMDescHeader = static_cast<PIMAGE_COR20_HEADER>(RVAToPtr(GetDirEntryRVA(IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR)));
		if (pCOMDescHeader == nullptr)
			return false;

		m_stCOR20Desc = { PtrToOffset(pCOMDescHeader), *pCOMDescHeader };
		m_stFileInfo.HasCOMDescr = true;

		return true;
	}
}