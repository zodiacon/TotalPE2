#include "GhidraDecompiler.h"

#include "libdecomp.hh"
#include "sleigh_arch.hh"
#include "loadimage.hh"
#include "funcdata.hh"
#include "database.hh"
#include "printlanguage.hh"
#include "printc.hh"
#include "marshal.hh"
#include "xml.hh"

#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace ghidra;

namespace {
	// The image as the decompiler reads it: bytes at virtual addresses, from the caller
	class CallbackLoadImage : public LoadImage {
	public:
		CallbackLoadImage(std::string const& target, GhidraDecompiler::ReadMemory read)
			: LoadImage("image"), m_Target(target), m_Read(std::move(read)) {}

		void loadFill(uint1* ptr, int4 size, const Address& addr) override {
			if (!m_Read(addr.getOffset(), ptr, (size_t)size)) {
				std::ostringstream s;
				addr.printRaw(s);
				throw DataUnavailError("No data at " + s.str());
			}
		}
		std::string getArchType() const override {
			return m_Target;
		}
		void adjustVma(long) override {}

	private:
		std::string m_Target;
		GhidraDecompiler::ReadMemory m_Read;
	};

	// A SLEIGH architecture whose image is read through the callback
	class CallbackArchitecture : public SleighArchitecture {
	public:
		CallbackArchitecture(std::string const& target, GhidraDecompiler::ReadMemory read, std::ostream* errors)
			: SleighArchitecture("image", target, errors), m_Read(std::move(read)), m_Target(target) {}

	protected:
		void buildLoader(DocumentStorage& store) override {
			collectSpecFiles(*errorstream);
			loader = new CallbackLoadImage(m_Target, m_Read);
		}

		// The basic types by the names C uses (as Ghidra's own user interface names them), not the library's
		// "int4" and "xunknown8". The first of a size and kind is the one the decompiler uses for it.
		void buildCoreTypes(DocumentStorage&) override {
			types->setCoreType("void", 1, TYPE_VOID, false);
			types->setCoreType("bool", 1, TYPE_BOOL, false);
			types->setCoreType("uchar", 1, TYPE_UINT, false);
			types->setCoreType("ushort", 2, TYPE_UINT, false);
			types->setCoreType("uint", 4, TYPE_UINT, false);
			types->setCoreType("ulonglong", 8, TYPE_UINT, false);
			types->setCoreType("char", 1, TYPE_INT, true);
			types->setCoreType("short", 2, TYPE_INT, false);
			types->setCoreType("int", 4, TYPE_INT, false);
			types->setCoreType("longlong", 8, TYPE_INT, false);
			types->setCoreType("float", 4, TYPE_FLOAT, false);
			types->setCoreType("double", 8, TYPE_FLOAT, false);
			types->setCoreType("float10", 10, TYPE_FLOAT, false);
			types->setCoreType("float16", 16, TYPE_FLOAT, false);
			types->setCoreType("undefined", 1, TYPE_UNKNOWN, false);
			types->setCoreType("undefined2", 2, TYPE_UNKNOWN, false);
			types->setCoreType("undefined4", 4, TYPE_UNKNOWN, false);
			types->setCoreType("undefined8", 8, TYPE_UNKNOWN, false);
			types->setCoreType("code", 1, TYPE_CODE, false);
			types->setCoreType("wchar_t", 2, TYPE_INT, true);
			types->setCoreType("wchar32", 4, TYPE_INT, true);
			types->cacheCoreTypes();
		}

	private:
		GhidraDecompiler::ReadMemory m_Read;
		std::string m_Target;
	};

	// The C printer, with strings: a constant that is not known to point to characters (a pointer-sized integer, or a
	// pointer to anything), but points into read-only memory at a string, is printed as the string. Ghidra does that for
	// pointers to characters only (its own analysis types the strings of a program first).
	class StringPrintC : public PrintC {
	public:
		using PrintC::PrintC;

	protected:
		void pushConstant(uintb val, const Datatype* ct, tagtype tag, const Varnode* vn, const PcodeOp* op, uint4 displayFormat) override {
			if (val != 0 && LooksLikePointer(ct)) {
				Datatype* charType = nullptr;
				Address addr;
				if (FindString(val, op, addr, charType)) {
					std::ostringstream str;
					if (printCharacterConstant(str, addr, charType)) {
						pushAtom(Atom(str.str(), vartoken, EmitMarkup::const_color, op, vn));
						return;
					}
				}
			}
			PrintC::pushConstant(val, ct, tag, vn, op, displayFormat);
		}

	private:
		bool LooksLikePointer(const Datatype* ct) const {
			auto size = glb->getDefaultDataSpace()->getAddrSize();
			switch (ct->getMetatype()) {
				case TYPE_UNKNOWN: case TYPE_INT: case TYPE_UINT:
					return ct->getSize() == size;
				case TYPE_PTR:
				{
					auto to = static_cast<const TypePointer*>(ct)->getPtrTo()->getMetatype();
					return to == TYPE_UNKNOWN || to == TYPE_VOID;
				}
				default:
					return false;
			}
		}

		// A string of at least a few printable characters (UTF-16 or 8 bit), terminated, in read-only memory
		bool FindString(uintb val, const PcodeOp* op, Address& addr, Datatype*& charType) const {
			auto space = glb->getDefaultDataSpace();
			uintb encoding;
			addr = glb->resolveConstant(space, val, space->getAddrSize(), op ? op->getAddr() : Address(), encoding);
			if (addr.isInvalid() || !glb->symboltab->getGlobalScope()->isReadOnly(addr, 1, Address()))
				return false;
			uint1 bytes[512];
			int4 size = 0;
			try {
				// as much as there is, up to the buffer's size
				for (int4 chunk = 64; size < (int4)sizeof(bytes); size += chunk)
					glb->loader->loadFill(bytes + size, chunk, addr + size);
			}
			catch (DataUnavailError const&) {
			}
			bool wide = size >= 4 && bytes[0] != 0 && bytes[1] == 0;
			int count = 0;
			bool terminated = false;
			for (int4 i = 0; i + (wide ? 1 : 0) < size; i += wide ? 2 : 1) {
				unsigned ch = wide ? bytes[i] | (bytes[i + 1] << 8) : bytes[i];
				if (ch == 0) {
					terminated = true;
					break;
				}
				bool printable = ch == '\t' || ch == '\r' || ch == '\n' || (ch >= 0x20 && ch < 0x7F) ||
					(wide && ch >= 0xA0 && (ch < 0xD800 || ch > 0xDFFF) && ch < 0xFFFE);
				if (!printable)
					return false;
				count++;
			}
			if (!terminated || count < 3)
				return false;
			charType = wide ? glb->types->findByName("wchar_t") : glb->types->getBase(1, TYPE_INT);
			return charType != nullptr;
		}
	};

	bool s_Initialized = false;

	// The scope of a name and the name in it. A qualified C++ name ("ns::C::F") is taken whole, in the global scope: it is
	// printed as it is, and a scope of its own (one per namespace or class) would have to own the addresses of its symbols,
	// which the global scope does.
	Scope* ScopeOf(Architecture* arch, std::string const& name, std::string& basename) {
		basename = name;
		return arch->symboltab->getGlobalScope();
	}

	// A name the caller did not give (an unnamed struct, union or enum): one made up from the id
	std::string NameOr(std::string const& name, char const* kind, uint32_t id) {
		if (!name.empty() && name[0] != '<' && name.find("<unnamed") == std::string::npos && name.find("__unnamed") == std::string::npos)
			return name;
		return std::string(kind) + "_" + std::to_string(id);
	}
}

struct GhidraDecompiler::Impl {
	std::string Target;
	ReadMemory Read;
	std::ostringstream Messages;	// what the decompiler says (warnings)
	std::unique_ptr<CallbackArchitecture> Arch;
	uint64_t NextExternal{ 0 };		// where the next import goes (0: no area for them)

	TypeSource Types;
	PrototypeSource Prototypes;
	NameSource Names;
	LocalSource Locals;
	std::unordered_map<uint32_t, Datatype*> Converted;	// the decompiler's types of the caller's ids
	std::unordered_set<uint64_t> Prototyped;			// the functions whose prototypes were asked for
	std::unordered_set<uint64_t> Localized;				// the functions whose local variables were asked for
	std::unordered_map<std::string, std::string> Fixups;	// the call fixups of the compiler, by the names of the functions they replace

	Datatype* Unknown(uint32_t size);
	// a field of a struct or union as it is defined: a bit field has bits
	struct Member {
		std::string Name;
		int4 Offset;		// in bytes (of a bit field: of its first byte)
		Datatype* Type;
		int4 Bits{ 0 };		// of a bit field: its size, and its first bit in the byte at the offset
		int4 First{ 0 };
	};
	void DefineFields(Datatype* type, char const* metatype, uint32_t size, int4 alignment, std::vector<Member> const& fields);
	Datatype* DefineEnum(std::string const& name, uint32_t size, bool isSigned, std::vector<std::pair<std::string, uint64_t>> const& values);
	// a named type of a kind that is there already (the symbols describe a type more than once: const and not); a name
	// that a type of another kind has gets the id after it
	Datatype* Existing(std::string& name, type_metatype meta, uint32_t id);
	Datatype* Convert(uint32_t id, int depth = 0);
	// Types are nested deeply (a class with containers of containers): a recursion that goes deeper than this is not a
	// real type (structs and unions end their own recursion, by name)
	static constexpr int MaxDepth = 1000;
	bool ApplyPrototype(Funcdata* fd);
	void ApplyLocals(Funcdata* fd);
	bool ApplyFixup(Funcdata* fd);
};

// Bytes of a size: the core type of the size, or an array of bytes
Datatype* GhidraDecompiler::Impl::Unknown(uint32_t size) {
	auto types = Arch->types;
	if (size == 1 || size == 2 || size == 4 || size == 8)
		return types->getBase((int4)size, TYPE_UNKNOWN);
	return types->getTypeArray(size == 0 ? 1 : (int4)size, types->getBase(1, TYPE_UNKNOWN));
}

// The fields of a struct or union that is defined by name only (a stub). The library sets fields from a description in
// its own format only (as Ghidra sends types): the description is written, and read back.
void GhidraDecompiler::Impl::DefineFields(Datatype* type, char const* metatype, uint32_t size, int4 alignment, std::vector<Member> const& fields) {
	std::ostringstream xml;
	XmlEncode encoder(xml);
	encoder.openElement(ELEM_TYPE);
	encoder.writeString(ATTRIB_NAME, type->getName());
	encoder.writeUnsignedInteger(ATTRIB_ID, type->getId());
	encoder.writeSignedInteger(ATTRIB_SIZE, (int4)size);
	encoder.writeString(ATTRIB_METATYPE, metatype);
	encoder.writeSignedInteger(ATTRIB_ALIGNMENT, alignment);
	for (size_t i = 0; i < fields.size(); i++) {
		auto const& f = fields[i];
		auto& element = f.Bits ? ELEM_BITFIELD : ELEM_FIELD;
		encoder.openElement(element);
		encoder.writeString(ATTRIB_NAME, f.Name);
		encoder.writeSignedInteger(ATTRIB_OFFSET, f.Offset);
		if (f.Bits) {
			encoder.writeSignedInteger(ATTRIB_ID, (int4)i);
			encoder.writeSignedInteger(ATTRIB_SIZE, f.Bits);
			encoder.writeSignedInteger(ATTRIB_FIRST, f.First);
		}
		else if ((int4)i != f.Offset)
			encoder.writeSignedInteger(ATTRIB_ID, (int4)i);
		f.Type->encodeRef(encoder);
		encoder.closeElement(element);
	}
	encoder.closeElement(ELEM_TYPE);

	std::istringstream in(xml.str());
	DocumentStorage store;
	auto doc = store.parseDocument(in);
	XmlDecode decoder(Arch.get(), doc->getRoot());
	Arch->types->decodeType(decoder);
}

// An enumeration of a size (the library makes them of one size only, unless it reads them): written, and read back
Datatype* GhidraDecompiler::Impl::DefineEnum(std::string const& name, uint32_t size, bool isSigned, std::vector<std::pair<std::string, uint64_t>> const& values) {
	std::ostringstream xml;
	XmlEncode encoder(xml);
	encoder.openElement(ELEM_TYPE);
	encoder.writeString(ATTRIB_NAME, name);
	// (the id is the hash of the name, as the library makes it when there is none)
	encoder.writeSignedInteger(ATTRIB_SIZE, (int4)size);
	encoder.writeString(ATTRIB_METATYPE, isSigned ? "enum_int" : "enum_uint");
	uint64_t mask = size >= 8 ? ~0ULL : (1ULL << (size * 8)) - 1;
	std::unordered_set<uint64_t> seen;
	for (auto const& [valueName, value] : values) {
		if (!seen.insert(value & mask).second)
			continue;	// the first name of a value
		encoder.openElement(ELEM_VAL);
		encoder.writeString(ATTRIB_NAME, valueName);
		encoder.writeUnsignedInteger(ATTRIB_VALUE, value & mask);
		encoder.closeElement(ELEM_VAL);
	}
	encoder.closeElement(ELEM_TYPE);

	std::istringstream in(xml.str());
	DocumentStorage store;
	auto doc = store.parseDocument(in);
	XmlDecode decoder(Arch.get(), doc->getRoot());
	return Arch->types->decodeType(decoder);
}

Datatype* GhidraDecompiler::Impl::Existing(std::string& name, type_metatype meta, uint32_t id) {
	auto existing = Arch->types->findByName(name);
	if (!existing)
		return nullptr;
	if (existing->getMetatype() == meta)
		return existing;
	name += "_" + std::to_string(id);
	existing = Arch->types->findByName(name);
	return existing && existing->getMetatype() == meta ? existing : nullptr;
}

// The decompiler's type of one of the caller's (made once). A struct or union is known (by name) before its fields are
// set, so that types that point to each other end.
Datatype* GhidraDecompiler::Impl::Convert(uint32_t id, int depth) {
	auto types = Arch->types;
	if (id == 0)
		return types->getTypeVoid();
	if (auto it = Converted.find(id); it != Converted.end())
		return it->second;
	Type t;
	if (!Types || depth > MaxDepth || !Types(id, t))
		return Unknown(t.Size);		// (not remembered: the same id may come again at a depth that is fine)

	Datatype* result = nullptr;
	try {
		switch (t.Kind) {
			case TypeKind::Void:
				result = types->getTypeVoid();
				break;

			case TypeKind::Integer:
				result = types->getBase(t.Size ? (int4)t.Size : 4, t.Signed ? TYPE_INT : TYPE_UINT);
				break;

			case TypeKind::Boolean:
				result = types->getBase(t.Size ? (int4)t.Size : 1, TYPE_BOOL);
				break;

			case TypeKind::Char:
				result = types->findByName(t.Size == 2 ? "wchar_t" : t.Size == 4 ? "wchar32" : "char");
				if (!result)
					result = types->getBase(t.Size ? (int4)t.Size : 1, TYPE_INT);
				break;

			case TypeKind::Float:
				result = types->getBase(t.Size ? (int4)t.Size : 8, TYPE_FLOAT);
				break;

			case TypeKind::Pointer:
			{
				auto target = Convert(t.Target, depth + 1);
				uint32_t size = t.Size ? t.Size : (uint32_t)Arch->getDefaultDataSpace()->getAddrSize();
				result = types->getTypePointer((int4)size, target, Arch->getDefaultDataSpace()->getWordSize());
				break;
			}

			case TypeKind::Array:
			{
				auto element = Convert(t.Target, depth + 1);
				if (element->getSize() <= 0 || t.Count == 0)
					result = Unknown(t.Size);
				else
					result = types->getTypeArray((int4)t.Count, element);
				break;
			}

			case TypeKind::Struct:
			{
				auto name = NameOr(t.Name, "struct", id);
				if (auto existing = Existing(name, TYPE_STRUCT, id)) {
					result = existing;
					break;
				}
				auto st = types->getTypeStruct(name);
				Converted[id] = st;
				if (st->isIncomplete() && t.Size > 0) {
					// in order, without overlaps (the members of unions without names are flat), inside the struct; bit
					// fields by their bits
					auto firstBit = [](Type::Field const& f) { return f.Offset * 8ULL + (f.Bits ? f.BitPosition : 0); };
					std::vector<Type::Field> sorted = t.Fields;
					std::stable_sort(sorted.begin(), sorted.end(), [&](auto& a, auto& b) { return firstBit(a) < firstBit(b); });
					std::vector<Member> fields;
					int4 alignment = 1;
					uint64_t end = 0;	// in bits
					for (auto const& f : sorted) {
						if (firstBit(f) < end)
							continue;
						auto ft = Convert(f.Type, depth + 1);
						if (ft->getSize() <= 0)
							continue;
						alignment = std::max(alignment, ft->getAlignment());
						if (f.Bits) {
							// of an integer (or a bool or an enum) only
							auto meta = ft->getMetatype();
							if (meta != TYPE_INT && meta != TYPE_UINT && meta != TYPE_BOOL && meta != TYPE_ENUM_INT && meta != TYPE_ENUM_UINT)
								continue;
							auto bit = firstBit(f);
							if ((bit + f.Bits + 7) / 8 > t.Size)
								continue;
							fields.push_back({ f.Name, (int4)(bit / 8), ft, (int4)f.Bits, (int4)(bit % 8) });
							end = bit + f.Bits;
						}
						else {
							if (f.Offset + (uint32_t)ft->getSize() > t.Size)
								continue;
							fields.push_back({ f.Name, (int4)f.Offset, ft });
							end = (f.Offset + (uint64_t)ft->getSize()) * 8;
						}
					}
					DefineFields(st, "struct", t.Size, alignment, fields);
				}
				result = st;
				break;
			}

			case TypeKind::Union:
			{
				auto name = NameOr(t.Name, "union", id);
				if (auto existing = Existing(name, TYPE_UNION, id)) {
					result = existing;
					break;
				}
				auto ut = types->getTypeUnion(name);
				Converted[id] = ut;
				if (ut->isIncomplete() && t.Size > 0) {
					std::vector<Member> fields;
					int4 alignment = 1;
					for (auto const& f : t.Fields) {
						if (f.Bits)
							continue;	// (a union's bit fields are rare: not shown)
						auto ft = Convert(f.Type, depth + 1);
						if (ft->getSize() <= 0 || f.Offset + (uint32_t)ft->getSize() > t.Size)
							continue;
						fields.push_back({ f.Name, (int4)f.Offset, ft });
						alignment = std::max(alignment, ft->getAlignment());
					}
					DefineFields(ut, "union", t.Size, alignment, fields);
				}
				result = ut;
				break;
			}

			case TypeKind::Enum:
			{
				auto name = NameOr(t.Name, "enum", id);
				auto meta = t.Signed ? TYPE_INT : TYPE_UINT;	// an enumeration's metatype is that of its values
				result = Existing(name, meta, id);
				if (!result)
					result = DefineEnum(name, t.Size ? t.Size : 4, t.Signed, t.Values);
				break;
			}

			case TypeKind::Typedef:
			{
				auto target = Convert(t.Target, depth + 1);
				try {
					result = t.Name.empty() ? target : types->getTypedef(target, t.Name, 0, 0);
				}
				catch (LowlevelError const&) {
					result = target;	// a name that is already another type's
				}
				break;
			}

			case TypeKind::Function:
			{
				PrototypePieces pieces;
				pieces.model = Arch->defaultfp;
				pieces.outtype = Convert(t.Target, depth + 1);
				for (auto parameter : t.Parameters) {
					pieces.intypes.push_back(Convert(parameter, depth + 1));
					pieces.innames.emplace_back();
				}
				pieces.firstVarArgSlot = t.Variadic ? (int4)pieces.intypes.size() : -1;
				result = types->getTypeCode(pieces);
				break;
			}

			default:
				result = Unknown(t.Size);
				break;
		}
	}
	catch (LowlevelError const& e) {
		Messages << "Type " << (t.Name.empty() ? std::to_string(id) : t.Name) << ": " << e.explain << "\n";
		result = nullptr;
	}
	catch (DecoderError const& e) {
		Messages << "Type " << (t.Name.empty() ? std::to_string(id) : t.Name) << ": " << e.explain << "\n";
		result = nullptr;
	}
	if (!result)
		result = Unknown(t.Size);
	Converted[id] = result;
	return result;
}

// The prototype of a function from the caller (once per function): its types and the names of its parameters, locked so
// that the decompiler keeps them. False if there is none (or it was set already).
bool GhidraDecompiler::Impl::ApplyPrototype(Funcdata* fd) {
	auto address = fd->getAddress().getOffset();
	if (!Prototypes || !Types || !Prototyped.insert(address).second)
		return false;
	uint32_t typeId = 0;
	std::vector<std::string> names;
	Type t;
	if (!Prototypes(address, typeId, names) || !Types(typeId, t) || t.Kind != TypeKind::Function)
		return false;
	try {
		PrototypePieces pieces;
		pieces.model = Arch->defaultfp;
		pieces.name = fd->getName();
		pieces.outtype = Convert(t.Target);
		for (size_t i = 0; i < t.Parameters.size(); i++) {
			auto type = Convert(t.Parameters[i]);
			if (type->getSize() <= 0)
				return false;	// a parameter of no size: the prototype is not usable
			pieces.intypes.push_back(type);
			pieces.innames.push_back(i < names.size() ? names[i] : std::string());
		}
		pieces.firstVarArgSlot = t.Variadic ? (int4)pieces.intypes.size() : -1;
		auto& proto = fd->getFuncProto();
		proto.setPieces(pieces);
		proto.setInputLock(true);
		proto.setOutputLock(true);
		return true;
	}
	catch (LowlevelError const&) {
		return false;
	}
}

// The local variables of a function from the caller (once per function): symbols of the function's scope on the stack,
// their names and types locked, as Ghidra's own symbols from a PDB are. One that overlaps another (a variable of an inner
// block in the same place as one of another block) is left out.
void GhidraDecompiler::Impl::ApplyLocals(Funcdata* fd) {
	auto address = fd->getAddress().getOffset();
	if (!Locals || !Localized.insert(address).second)
		return;
	std::vector<Local> locals;
	if (!Locals(address, locals))
		return;
	auto stack = Arch->getStackSpace();
	if (!stack)
		return;
	auto scope = fd->getScopeLocal();
	for (auto const& local : locals) {
		if (local.Name.empty())
			continue;
		try {
			auto type = Convert(local.Type);
			if (type->getSize() <= 0)
				continue;
			Address addr(stack, stack->wrapOffset((uintb)local.Offset));
			std::vector<Symbol*> named;
			scope->findByName(local.Name, named);
			if (!named.empty() || scope->findOverlap(addr, type->getSize()))
				continue;
			auto sym = scope->addSymbol(local.Name, type, addr, Address())->getSymbol();
			scope->setAttribute(sym, Varnode::namelock | Varnode::typelock);
		}
		catch (LowlevelError const&) {
			// one the decompiler does not take: the variable gets a name of its own
		}
	}
}

// A function that the compiler's specification replaces at its calls (by p-code of what the call does: one that moves the
// stack pointer, a prologue that is called...): true if it was not replaced before
bool GhidraDecompiler::Impl::ApplyFixup(Funcdata* fd) {
	auto it = Fixups.find(fd->getName());
	if (it == Fixups.end() || fd->getFuncProto().isInline())
		return false;
	auto id = Arch->pcodeinjectlib->getPayloadId(InjectPayload::CALLFIXUP_TYPE, it->second);
	if (id < 0)
		return false;
	fd->getFuncProto().setInjectId(id);
	return true;
}

bool GhidraDecompiler::Initialize(std::wstring const& languagesFolder, std::string& error) {
	if (s_Initialized)
		return true;
	try {
		// the decompiler's file names are narrow: the folder is expected to be ASCII
		auto narrow = [](std::wstring const& text) {
			std::string result;
			for (auto ch : text)
				result += (char)ch;
			return result;
		};
		std::vector<std::string> paths{ narrow(languagesFolder) };
		std::error_code ec;
		for (auto const& entry : std::filesystem::directory_iterator(languagesFolder, ec))
			if (entry.is_directory())
				paths.push_back(narrow(entry.path().wstring()));
		startDecompilerLibrary(paths);
		s_Initialized = true;
		return true;
	}
	catch (LowlevelError const& e) {
		error = e.explain;
	}
	return false;
}

GhidraDecompiler::GhidraDecompiler(std::string const& target, ReadMemory read) : m_Impl(std::make_unique<Impl>()) {
	m_Impl->Target = target;
	m_Impl->Read = std::move(read);
}

GhidraDecompiler::~GhidraDecompiler() = default;

bool GhidraDecompiler::Open(std::string& error) {
	if (m_Impl->Arch)
		return true;
	if (!s_Initialized) {
		error = "The decompiler is not initialized";
		return false;
	}
	try {
		auto arch = std::make_unique<CallbackArchitecture>(m_Impl->Target, m_Impl->Read, &m_Impl->Messages);
		DocumentStorage store;
		arch->init(store);
		// the printer of C with strings, in the place of Ghidra's (with its settings)
		auto print = new StringPrintC(arch.get(), "c-language");
		print->setOutputStream(arch->print->getOutputStream());
		print->initializeFromArchitecture();
		print->setBraceFormatFunction(Emit::next_line);	// the function's brace right below its declaration (not after a blank line)
		arch->printlist.push_back(print);
		arch->print = print;
		print->adjustTypeOperators();
		// The functions that the compiler's call fixups are for: the library registers the fixups by their names only
		// (Ghidra itself gives them to the functions by name)
		if (auto spec = store.getTag("compiler_spec")) {
			for (auto fixup : spec->getChildren()) {
				if (fixup->getName() != "callfixup")
					continue;
				try {
					auto const& name = fixup->getAttributeValue("name");
					for (auto target : fixup->getChildren())
						if (target->getName() == "target")
							m_Impl->Fixups[target->getAttributeValue("name")] = name;
				}
				catch (DecoderError const&) {
				}
			}
		}
		m_Impl->Arch = std::move(arch);
		return true;
	}
	catch (LowlevelError const& e) {
		error = e.explain;
	}
	catch (DecoderError const& e) {
		error = e.explain;
	}
	return false;
}

void GhidraDecompiler::AddFunction(uint64_t address, std::string const& name) {
	auto arch = m_Impl->Arch.get();
	if (!arch || name.empty())
		return;
	try {
		Address addr(arch->getDefaultCodeSpace(), address);
		auto global = arch->symboltab->getGlobalScope();
		if (global->queryFunction(addr))
			return;
		std::string basename;
		auto scope = ScopeOf(arch, name, basename);
		scope->addFunction(addr, basename);
	}
	catch (LowlevelError const&) {
		// a name the decompiler does not take: the function keeps a name of its own
	}
}

void GhidraDecompiler::AddReadOnly(uint64_t address, uint64_t size) {
	auto arch = m_Impl->Arch.get();
	if (!arch || size == 0)
		return;
	auto space = arch->getDefaultDataSpace();
	arch->symboltab->setPropertyRange(Varnode::readonly, Range(space, address, address + size - 1));
}

void GhidraDecompiler::SetExternalArea(uint64_t address) {
	m_Impl->NextExternal = address;
}

// The function an import is: a function without code (outside the image), and an external reference from the slot to it,
// which makes a call through the slot a call of the function
void GhidraDecompiler::AddImport(uint64_t slot, std::string const& name) {
	auto arch = m_Impl->Arch.get();
	if (!arch || name.empty() || m_Impl->NextExternal == 0)
		return;
	try {
		Address target(arch->getDefaultCodeSpace(), m_Impl->NextExternal);
		m_Impl->NextExternal += 16;
		auto global = arch->symboltab->getGlobalScope();
		global->addFunction(target, name)->getFunction()->setNoCode(true);
		global->addExternalRef(Address(arch->getDefaultDataSpace(), slot), target, name);
	}
	catch (LowlevelError const&) {
		// a name the decompiler does not take: the call stays a call through the pointer
	}
}

void GhidraDecompiler::SetTypeSource(TypeSource source) {
	m_Impl->Types = std::move(source);
}

void GhidraDecompiler::SetPrototypeSource(PrototypeSource source) {
	m_Impl->Prototypes = std::move(source);
}

void GhidraDecompiler::SetNameSource(NameSource source) {
	m_Impl->Names = std::move(source);
}

void GhidraDecompiler::SetLocalSource(LocalSource source) {
	m_Impl->Locals = std::move(source);
}

void GhidraDecompiler::AddGlobal(uint64_t address, std::string const& name, uint32_t type) {
	auto arch = m_Impl->Arch.get();
	if (!arch || name.empty())
		return;
	try {
		Address addr(arch->getDefaultDataSpace(), address);
		if (arch->symboltab->getGlobalScope()->queryFunction(addr))
			return;		// code, not a variable
		auto ct = m_Impl->Convert(type);
		if (ct->getSize() <= 0)
			return;
		std::string basename;
		auto scope = ScopeOf(arch, name, basename);
		scope->addSymbol(basename, ct, addr, Address());
	}
	catch (LowlevelError const&) {
		// one that the decompiler does not take (overlapping another): it goes without
	}
}

namespace {
	// The code as the printer marks it up (packed: each token an element, the operation it shows by its time), as text and the
	// addresses of each line: those of the operations, labels and comments of the line; the prototype is at the entry.
	class MarkupReader {
	public:
		MarkupReader(Funcdata const* fd, std::string& text, GhidraDecompiler::LineAddresses& lines) : m_Text(text), m_Lines(lines) {
			for (auto it = fd->beginOpAll(); it != fd->endOpAll(); ++it)
				m_OpAddress.emplace(it->second->getTime(), it->second->getAddr().getOffset());
			m_Entry = fd->getAddress().getOffset();
			m_Lines.emplace_back();
		}

		// an element (a token, or a part of the code: a statement, a block...) and what is in it
		void Read(Decoder& decoder, bool proto = false) {
			auto id = decoder.openElement();
			if (id == ELEM_BREAK.getId()) {
				int indent = 0;
				for (uint4 attr; (attr = decoder.getNextAttributeId()) != 0; )
					if (attr == ATTRIB_INDENT.getId())
						indent = (int)decoder.readSignedInteger();
				m_Text += '\n';
				m_Lines.emplace_back();
				m_Text.append((size_t)std::max(0, indent), ' ');
				decoder.closeElement(id);
				return;
			}
			proto |= id == ELEM_FUNCPROTO.getId();
			bool located = id == ELEM_LABEL.getId() || id == ELEM_COMMENT.getId();
			for (uint4 attr; (attr = decoder.getNextAttributeId()) != 0; ) {
				if (attr == ATTRIB_OPREF.getId()) {
					if (auto it = m_OpAddress.find((uintm)decoder.readUnsignedInteger()); it != m_OpAddress.end())
						Add(it->second);
				}
				else if (attr == ATTRIB_OFF.getId() && located)
					Add(decoder.readUnsignedInteger());
				else if (attr == ATTRIB_CONTENT.getId()) {
					m_Text += decoder.readString();
					if (proto)
						Add(m_Entry);
				}
			}
			while (decoder.peekElement() != 0)
				Read(decoder, proto);
			decoder.closeElement(id);
		}

	private:
		void Add(uint64_t address) {
			auto& line = m_Lines.back();
			if (std::find(line.begin(), line.end(), address) == line.end())
				line.push_back(address);
		}

		std::string& m_Text;
		GhidraDecompiler::LineAddresses& m_Lines;
		std::unordered_map<uintm, uint64_t> m_OpAddress;
		uint64_t m_Entry;
	};
}

bool GhidraDecompiler::Decompile(uint64_t address, std::string const& name, std::string& code, std::string& error, FunctionRefs* functions,
	LineAddresses* lines) {
	if (!Open(error))
		return false;
	auto arch = m_Impl->Arch.get();
	try {
		Address addr(arch->getDefaultCodeSpace(), address);
		auto global = arch->symboltab->getGlobalScope();
		auto fd = global->queryFunction(addr);
		if (!fd) {
			std::string fname = name;
			if (fname.empty() && m_Impl->Names)
				fname = m_Impl->Names(address);
			if (fname.empty())
				arch->nameFunction(addr, fname);
			std::string basename;
			auto scope = ScopeOf(arch, fname, basename);
			fd = scope->addFunction(addr, basename)->getFunction();
		}
		if (fd->isProcStarted())
			arch->clearAnalysis(fd);
		m_Impl->Messages.str({});		// what is said about this function
		m_Impl->ApplyPrototype(fd);
		m_Impl->ApplyLocals(fd);

		auto action = arch->allacts.getCurrent();
		action->reset(*fd);
		int4 result = action->perform(*fd);

		// the names and prototypes of the functions it calls: if there are any, it is decompiled again with them
		bool more = false;
		for (int4 i = 0; i < fd->numCalls(); i++) {
			auto fc = fd->getCallSpecs(i);
			auto const& entry = fc->getEntryAddress();
			if (entry.isInvalid())
				continue;
			auto callee = fc->getFuncdata();
			if (!callee)
				callee = global->queryFunction(entry);
			if (!callee && m_Impl->Names) {
				auto calleeName = m_Impl->Names(entry.getOffset());
				if (!calleeName.empty()) {
					AddFunction(entry.getOffset(), calleeName);
					callee = global->queryFunction(entry);
					more |= callee != nullptr;
				}
			}
			if (callee && m_Impl->ApplyPrototype(callee))
				more = true;
			if (callee && m_Impl->ApplyFixup(callee))
				more = true;
		}
		if (more) {
			arch->clearAnalysis(fd);
			action->reset(*fd);
			result = action->perform(*fd);
		}

		std::ostringstream out;
		if (result < 0)
			out << "/* The decompiler stopped before it was done */\n";
		// the decompiler's messages (types it could not take, warnings), as a comment
		if (auto messages = m_Impl->Messages.str(); !messages.empty()) {
			out << "/*\n";
			std::istringstream lines(messages);
			for (std::string line; std::getline(lines, line); )
				if (!line.empty())
					out << " * " << line << "\n";
			out << " */\n";
		}
		if (lines) {
			// marked up: the text is read back from it, with the addresses of its lines (the lines of the comment above have none)
			lines->clear();
			auto header = out.str();
			for (auto ch : header)
				if (ch == '\n')
					lines->emplace_back();
			std::ostringstream markup;
			arch->print->setMarkup(true);
			arch->print->setOutputStream(&markup);
			try {
				arch->print->docFunction(fd);
			}
			catch (...) {
				arch->print->setMarkup(false);
				throw;
			}
			arch->print->setMarkup(false);
			std::istringstream in(markup.str());
			PackedDecode decoder(arch);
			decoder.ingestStream(in);
			std::string text;
			GhidraDecompiler::LineAddresses body;
			MarkupReader reader(fd, text, body);
			while (decoder.peekElement() != 0)
				reader.Read(decoder);
			code = header + text;
			if (!code.empty() && code.back() != '\n')
				code += '\n';
			lines->insert(lines->end(), body.begin(), body.end());
		}
		else {
			arch->print->setOutputStream(&out);
			arch->print->docFunction(fd);
			code = out.str();
		}

		if (functions) {
			functions->clear();
			functions->emplace_back(fd->getName(), address);
			for (int4 i = 0; i < fd->numCalls(); i++) {
				auto fc = fd->getCallSpecs(i);
				auto const& entry = fc->getEntryAddress();
				if (entry.isInvalid())
					continue;
				if (!fc->getName().empty())
					functions->emplace_back(fc->getName(), entry.getOffset());
				if (auto callee = fc->getFuncdata(); callee && callee->getName() != fc->getName())
					functions->emplace_back(callee->getName(), entry.getOffset());
			}
		}
		return true;
	}
	catch (LowlevelError const& e) {
		error = e.explain;
	}
	catch (DecoderError const& e) {
		error = e.explain;
	}
	return false;
}
