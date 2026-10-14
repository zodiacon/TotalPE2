#pragma once

#include <string>
#include <vector>
#include <atlcomcli.h>


struct IDiaSymbol;

class DiaSymbol {
	friend class DiaSession;
public:
    operator bool() const;

    uint32_t TimeStamp() const;
    std::wstring Name() const;
    std::wstring UndecoratedName() const;
    std::wstring SourceFileName() const;
    std::wstring ObjectFileName() const;
    std::wstring CompilerName() const;
    uint32_t Id() const;
    uint32_t Age() const;
    int32_t Offset() const;
    AccessMode Access() const;
    DiaSymbol ClassParent() const;
    std::wstring LibraryName() const;
    DiaSymbol LexicalParent() const;
    DiaSymbol Type() const;
    DiaSymbol ArrayIndexType() const;
    DiaSymbol BaseSymbol() const;

    LocationKind Location() const;
    SymbolTag Tag() const;
    uint64_t Length() const;
    DataItemKind Kind() const;
    CodeLanguage Language() const;
    uint32_t AddressSection() const;
    uint32_t AddressOffset() const;
    uint64_t VirtualAddress() const;
    uint32_t RelativeVirtualAddress() const;
    uint32_t Slot() const;
    uint32_t Signature() const;
    uint32_t OffsetInUdt() const;
    GUID Guid() const;

    bool IsConst() const;
    bool IsVolatile() const;
    bool IsVirtual() const;
    bool IsIntrinsic() const;
    bool IsUnalignedType() const;
    bool IsPacked() const;
    bool IsScoped() const;
    bool IsPureVirtual() const;
    bool IsReference() const;
    bool IsNested() const;
    bool IsCode() const;
    bool IsFunction() const;
    bool IsManaged() const;
    bool IsIntroVirtual() const;
    bool IsEditAndContinueEnabled() const;
    bool IsCompilerGenerated() const;
    bool HasConstructor() const;
    bool HasNestedTypes() const;

    uint32_t Count() const;
    VARIANT Value() const;
    uint32_t TypeId() const;
    uint32_t BitPosition() const;
    uint32_t RegisterId() const;
    uint32_t FrameSize() const;
    UdtType UdtKind() const;
    SimpleType Simple() const;
    std::wstring TypeName() const;
    DiaSymbol ObjectPointerType() const;
    uint32_t ClassParentId() const;
    int32_t GetFieldOffset(std::wstring_view name, CompareOptions options = CompareOptions::None) const;

    std::vector<DiaSymbol> FindChildren(PCWSTR name = nullptr, SymbolTag tag = SymbolTag::Null, CompareOptions options = CompareOptions::None) const;
    // the children where they are at an address (a local of optimized code is in a place of its own in each of its ranges)
    std::vector<DiaSymbol> FindChildrenAt(uint32_t rva, SymbolTag tag = SymbolTag::Null) const;
    // where a child that FindChildrenAt finds is in its place: false if it is not known
    bool LiveRange(uint32_t& rva, uint64_t& length) const;

    static std::wstring SimpleTypeToString(SimpleType type, DWORD len);

protected:
	DiaSymbol(IDiaSymbol* sym = nullptr);

	CComPtr<IDiaSymbol> m_spSym;

public:
    static DiaSymbol Empty;
};

