#pragma once

#include <cstdint>
#include <string>
#include <vector>

class PEFile;
class ClrMetadata;

//
// C# from the IL of a .NET file. The IL is turned into expressions and statements (from what each instruction does to the
// evaluation stack), and its flow of control into if/else, loops, switch and try/catch/finally (from the dominators of
// the blocks); what does not fit those is written with goto. Names of locals are made up from their types (the metadata
// has none), and some of what the compiler writes in IL is written back as C# writes it: properties, events, operators,
// string concatenation, ?: and && / ||, compound assignments, for loops.
//
// 'tokens', if given, receives the token that each line refers to (0 for none), as the IL disassembly does: what a call or
// field access on the line is, a member's own token on its declaration.
//

// A method: its attributes, declaration and body
std::wstring DecompileClrMethod(PEFile const& pe, ClrMetadata const& md, uint32_t method, std::vector<uint32_t>* tokens = nullptr);
// A type: its declaration, fields, properties, events, methods and nested types
std::wstring DecompileClrType(PEFile const& pe, ClrMetadata const& md, uint32_t type, std::vector<uint32_t>* tokens = nullptr);
// A property or event (rows): its declaration and accessors
std::wstring DecompileClrProperty(PEFile const& pe, ClrMetadata const& md, uint32_t property, std::vector<uint32_t>* tokens = nullptr);
std::wstring DecompileClrEvent(PEFile const& pe, ClrMetadata const& md, uint32_t event, std::vector<uint32_t>* tokens = nullptr);
// A field's declaration
std::wstring DecompileClrField(ClrMetadata const& md, uint32_t field, std::vector<uint32_t>* tokens = nullptr);
// A type's declaration alone: "public class Circle : IShape"
std::wstring CSharpTypeDeclaration(ClrMetadata const& md, uint32_t type);
