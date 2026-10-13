#include "TestCommon.h"
#include <ClrMetadata.h>
#include <ClrDecompiler.h>
#include <ClrIL.h>
#include <fstream>
#include <iostream>
#include <random>

// Prints the C# of the types whose names contain TOTALPE_CS_TYPE in TOTALPE_CS_FILE
TEST_CASE("Print the C# of types", "[.cs]") {
	WCHAR file[MAX_PATH]{}, name[256]{};
	if (!::GetEnvironmentVariableW(L"TOTALPE_CS_FILE", file, MAX_PATH) || !::GetEnvironmentVariableW(L"TOTALPE_CS_TYPE", name, 256))
		SKIP("set TOTALPE_CS_FILE and TOTALPE_CS_TYPE");
	PEFile pe;
	REQUIRE(pe.Open(file));
	ClrMetadata md;
	REQUIRE(md.Parse(pe));
	for (uint32_t t = 1; t <= md.Types().size(); t++)
		if (md.Types()[t - 1].Enclosing == 0 && md.FullName(t).find(name) != std::wstring::npos)
			std::cout << Narrow(DecompileClrType(pe, md, t)) << std::endl;
}

namespace {
	std::wstring FrameworkFile(PCWSTR name) {
		WCHAR windows[MAX_PATH];
		::GetWindowsDirectoryW(windows, MAX_PATH);
		return std::wstring(windows) + L"\\Microsoft.NET\\Framework64\\v4.0.30319\\" + name;
	}

	// what the decompiler is tested on: C# of most kinds (compiled when the tests run, see CompiledSample)
	constexpr char const* SampleSource = R"cs(using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;

namespace Sample
{
	public enum Color { Red, Green = 5, Blue }

	[Flags]
	public enum Access : byte { None = 0, Read = 1, Write = 2 }

	[AttributeUsage(AttributeTargets.Class | AttributeTargets.Method, AllowMultiple = true)]
	public sealed class NoteAttribute : Attribute
	{
		public NoteAttribute(string text, Color color) { Text = text; Color = color; }
		public string Text { get; private set; }
		public Color Color { get; set; }
		public int Priority;
		public Type Related;
	}

	public interface IShape
	{
		double Area { get; }
		event EventHandler Changed;
	}

	[Note("a shape", Color.Blue, Priority = 3, Related = typeof(IShape))]
	[Serializable]
	public class Circle : IShape
	{
		private double _radius;
		public const int Sides = 0;
		public static readonly string Name = "circle";

		public Circle(double radius) { _radius = radius; }

		public double Radius
		{
			get { return _radius; }
			set
			{
				if (value < 0)
					throw new ArgumentOutOfRangeException("value");
				_radius = value;
				OnChanged();
			}
		}

		public double Area { get { return Math.PI * _radius * _radius; } }

		public event EventHandler Changed;

		protected virtual void OnChanged()
		{
			var handler = Changed;
			if (handler != null)
				handler(this, EventArgs.Empty);
		}

		public int this[int index] { get { return index * 2; } }

		public override string ToString() { return "Circle " + _radius; }

		public static Circle operator +(Circle a, Circle b) { return new Circle(a._radius + b._radius); }
	}

	public static class Algorithms
	{
		// enums: of other assemblies, [Flags] ones, 0
		public static FileStream Open(string path)
		{
			return File.Open(path, FileMode.OpenOrCreate, FileAccess.Read);
		}

		public static bool Same(string a, string b)
		{
			return string.Equals(a, b, StringComparison.OrdinalIgnoreCase);
		}

		public static int Mode(FileAttributes attributes, Access access)
		{
			if ((attributes & FileAttributes.Hidden) != 0)
				return 1;
			if (access == (Access.Read | Access.Write))
				return 2;
			if ((access & ~Access.Write) == Access.Read)
				return 5;
			return attributes == FileAttributes.Normal ? 3 : 4;
		}

		public static int Sum(int[] values)
		{
			int sum = 0;
			for (int i = 0; i < values.Length; i++)
				sum += values[i];
			return sum;
		}

		public static int Classify(int n)
		{
			switch (n)
			{
				case 0: return 10;
				case 1: return 20;
				case 2: return 30;
				case 3: return 40;
				default: return -1;
			}
		}

		public static string Describe(int x, bool verbose)
		{
			string result = x > 0 && verbose ? "positive" : "other";
			if (x < 0 || x > 100)
				result += "!";
			else if (x == 50)
				result = "half";
			else
				result = result.ToUpper();
			return result;
		}

		public static int Count(string text, char c)
		{
			int count = 0;
			int i = 0;
			while (i < text.Length)
			{
				if (text[i] == c)
					count++;
				i++;
			}
			return count;
		}

		public static int Repeat(int n)
		{
			int k = 0;
			do
			{
				k += n;
				n--;
			} while (n > 0);
			return k;
		}

		public static string Guarded(string s)
		{
			try
			{
				if (s.Length > 3)
					throw new InvalidOperationException("too long");
				return s.ToUpperInvariant();
			}
			catch (InvalidOperationException e)
			{
				return e.Message;
			}
			catch (Exception)
			{
				return "caught";
			}
			finally
			{
				Console.WriteLine("finally");
			}
		}

		public static bool IsEmpty(object o)
		{
			var list = o as List<int>;
			return list != null && list.Count == 0;
		}

		public static int Locked(object gate, List<int> list)
		{
			lock (gate)
			{
				return list.Count;
			}
		}

		public static long Length(string path)
		{
			using (var stream = File.OpenRead(path))
			{
				return stream.Length;
			}
		}

		[DllImport("kernel32.dll", SetLastError = true)]
		public static extern bool Beep(uint frequency, uint duration);

		public static Func<int, int> Adder(int k)
		{
			return x => x + k;
		}

		public static void Fill(Dictionary<string, int> map, IEnumerable<string> keys)
		{
			foreach (var key in keys)
			{
				if (!map.ContainsKey(key))
					map[key] = key.Length;
			}
		}
	}
}
)cs";
}

// The values of an enum that a value is made of: equal to it, or (of [Flags]) whose | it is
TEST_CASE("Enum values are named", "[clr][cs]") {
	ClrEnumInfo info;
	info.Values = { { L"None", 0 }, { L"Read", 1 }, { L"Write", 2 }, { L"ReadWrite", 3 }, { L"Delete", 8 } };
	CHECK(info.Parts(2) == std::vector<size_t>{ 2 });
	CHECK(info.Parts(9).empty());	// not [Flags]
	info.Flags = true;
	CHECK(info.Parts(0) == std::vector<size_t>{ 0 });
	CHECK(info.Parts(11) == std::vector<size_t>{ 3, 4 });	// ReadWrite | Delete, not Read | Write | Delete
	CHECK(info.Parts(9) == std::vector<size_t>{ 1, 4 });
	CHECK(info.Parts(16).empty());	// a bit that has no name
	// compared in the size of the enum: -1 is a uint's 0xFFFFFFFF
	ClrEnumInfo all;
	all.Underlying = 0x09;
	all.Values = { { L"All", 0xFFFFFFFF } };
	CHECK(all.Parts(-1) == std::vector<size_t>{ 0 });
}

// A library of .NET (most of them) refers to the types of System.Runtime, which forwards them to System.Private.CoreLib
TEST_CASE("Types of other assemblies are found through type forwarders", "[clr][system]") {
	WCHAR programs[MAX_PATH];
	if (!::GetEnvironmentVariableW(L"ProgramFiles", programs, MAX_PATH))
		SKIP("no Program Files");
	std::wstring path;
	auto shared = std::wstring(programs) + L"\\dotnet\\shared\\Microsoft.NETCore.App\\";
	WIN32_FIND_DATAW fd;
	if (auto find = ::FindFirstFileW((shared + L"*").c_str(), &fd); find != INVALID_HANDLE_VALUE) {
		do {
			auto file = shared + fd.cFileName + L"\\System.Text.RegularExpressions.dll";
			if (fd.cFileName[0] != L'.' && ::GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES)
				path = file;
		} while (::FindNextFileW(find, &fd));
		::FindClose(find);
	}
	PEFile pe;
	if (path.empty() || !pe.Open(path))
		SKIP("no .NET runtime");
	ClrMetadata md;
	REQUIRE(md.Parse(pe));
	uint32_t object = 0, comparison = 0;
	for (uint32_t row = 1; row <= md.Rows(ClrTableId::TypeRef); row++) {
		auto name = md.TypeName((ClrTableId::TypeRef << 24) | row);
		if (name == L"[System.Runtime]System.Object")
			object = (ClrTableId::TypeRef << 24) | row;
		else if (name == L"[System.Runtime]System.StringComparison")
			comparison = (ClrTableId::TypeRef << 24) | row;
	}
	REQUIRE(object);
	auto [other, type] = md.ResolveType(object);
	REQUIRE(other);
	CHECK(other->Assembly().Name == L"System.Private.CoreLib");
	CHECK(other->FullName(type) == L"System.Object");
	CHECK_FALSE(md.EnumInfo(object));
	if (comparison) {
		auto info = md.EnumInfo(comparison);
		REQUIRE(info);
		CHECK(info->Name == L"StringComparison");
		REQUIRE(info->Parts(5).size() == 1);
		CHECK(info->Values[info->Parts(5)[0]].Name == L"OrdinalIgnoreCase");
		CHECK(info->Values[0].Field == 0);	// not a field of this file
	}
}

// Every type of a large library: nothing fails, and little is left as goto
TEST_CASE("Every type of mscorlib decompiles", "[clr][cs][system]") {
	PEFile pe;
	auto path = FrameworkFile(L"mscorlib.dll");
	if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES || !pe.Open(path))
		SKIP("no mscorlib");
	ClrMetadata md;
	REQUIRE(md.Parse(pe));
	size_t methods = 0, gotos = 0, failed = 0, lines = 0;
	auto start = ::GetTickCount64();
	for (uint32_t m = 1; m <= md.Methods().size(); m++) {
		auto text = DecompileClrMethod(pe, md, m);
		methods++;
		if (text.find(L"goto ") != std::wstring::npos)
			gotos++;
		if (text.find(L"// the decompiler failed") != std::wstring::npos || text.find(L"not decompiled") != std::wstring::npos ||
			text.find(L"stack is empty") != std::wstring::npos)
			failed++;
		lines += std::count(text.begin(), text.end(), L'\n');
	}
	auto elapsed = ::GetTickCount64() - start;
	std::cout << std::format("{} methods, {} lines, {} with goto, {} failed, {} ms", methods, lines, gotos, failed, elapsed) << std::endl;
	CHECK(methods > 20000);
	CHECK(failed == 0);
	CHECK(gotos * 50 < methods);	// under 2% (most are switches on strings, by their hashes)
}

// Prints the methods of mscorlib that are left with goto (TOTALPE_CS_SKIP of them first, then TOTALPE_CS_COUNT), but for the
// switches on strings by their hashes
TEST_CASE("Print the methods left with goto", "[.cs]") {
	PEFile pe;
	auto path = FrameworkFile(L"mscorlib.dll");
	REQUIRE(pe.Open(path));
	ClrMetadata md;
	REQUIRE(md.Parse(pe));
	WCHAR value[32]{};
	int skip = ::GetEnvironmentVariableW(L"TOTALPE_CS_SKIP", value, 32) ? _wtoi(value) : 0;
	int count = ::GetEnvironmentVariableW(L"TOTALPE_CS_COUNT", value, 32) ? _wtoi(value) : 10;
	for (uint32_t m = 1; m <= md.Methods().size() && count > 0; m++) {
		auto text = DecompileClrMethod(pe, md, m);
		if (text.find(L"goto ") == std::wstring::npos || text.find(L"ComputeStringHash") != std::wstring::npos || skip-- > 0)
			continue;
		count--;
		std::cout << "// " << Narrow(md.FullName(md.Methods()[m - 1].Type)) << std::endl << Narrow(text) << std::endl;
		std::cout << Narrow(DisassembleIlMethod(pe, md, m)) << std::endl;
	}
}

namespace {
	// The sample (Sample.cs), compiled by the C# compiler of the .NET Framework (which Windows has) into a library, optimized or
	// not; an empty path if there is no compiler
	std::wstring CompiledSample(bool optimize) {
		static std::wstring paths[2];
		auto& path = paths[optimize];
		if (!path.empty())
			return path;
		auto csc = FrameworkFile(L"csc.exe");
		if (::GetFileAttributesW(csc.c_str()) == INVALID_FILE_ATTRIBUTES)
			return {};
		WCHAR temp[MAX_PATH];
		::GetTempPathW(MAX_PATH, temp);
		auto folder = std::wstring(temp) + std::format(L"TotalPE.Tests.{}\\", ::GetCurrentProcessId());
		::CreateDirectoryW(folder.c_str(), nullptr);
		auto source = folder + L"Sample.cs";
		{
			std::ofstream out(source, std::ios::binary);
			out << SampleSource;
		}
		auto dll = folder + (optimize ? L"SampleRelease.dll" : L"SampleDebug.dll");
		auto command = std::format(L"\"{}\" /nologo /target:library {} /out:\"{}\" \"{}\"", csc, optimize ? L"/optimize+" : L"/optimize-", dll, source);
		STARTUPINFOW si{ sizeof(si) };
		PROCESS_INFORMATION pi{};
		if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, folder.c_str(), &si, &pi))
			return {};
		::WaitForSingleObject(pi.hProcess, 60000);
		DWORD code = 1;
		::GetExitCodeProcess(pi.hProcess, &code);
		::CloseHandle(pi.hProcess);
		::CloseHandle(pi.hThread);
		if (code != 0)
			return {};
		return path = dll;
	}

	uint32_t FindTypeNamed(ClrMetadata const& md, std::wstring_view name) {
		for (uint32_t t = 1; t <= md.Types().size(); t++)
			if (md.Types()[t - 1].Name == name)
				return t;
		return 0;
	}

	uint32_t FindMethodNamed(ClrMetadata const& md, uint32_t type, std::wstring_view name) {
		for (auto m : md.MethodsOf(type))
			if (md.Methods()[m - 1].Name == name)
				return m;
		return 0;
	}
}

TEST_CASE("Properties, events and custom attributes are read", "[clr][cs]") {
	auto path = CompiledSample(true);
	if (path.empty())
		SKIP("no C# compiler of the .NET Framework");
	PEFile pe;
	REQUIRE(pe.Open(path));
	ClrMetadata md;
	REQUIRE(md.Parse(pe));
	auto circle = FindTypeNamed(md, L"Circle");
	REQUIRE(circle);

	std::vector<std::wstring> names;
	for (auto p : md.PropertiesOf(circle))
		names.push_back(md.Properties()[p - 1].Name);
	CHECK(names == std::vector<std::wstring>{ L"Radius", L"Area", L"Item" });
	auto radius = md.PropertiesOf(circle)[0];
	auto& property = md.Properties()[radius - 1];
	CHECK(property.Getter == FindMethodNamed(md, circle, L"get_Radius"));
	CHECK(property.Setter == FindMethodNamed(md, circle, L"set_Radius"));
	CHECK(md.Properties()[md.PropertiesOf(circle)[1] - 1].Setter == 0);
	CHECK(md.AccessorOf(property.Getter) == std::pair<uint32_t, ClrAccessor>{ (ClrTableId::Property << 24) | radius, ClrAccessor::Getter });
	CHECK(md.AccessorOf(FindMethodNamed(md, circle, L"ToString")).second == ClrAccessor::None);
	CHECK(md.PropertySignature(radius) == L"float64 Radius");
	CHECK(md.PropertyDeclaration(radius) == L".property instance float64 Radius()");
	CHECK(md.PropertySignature(md.PropertiesOf(circle)[2]) == L"int32 Item(int32)");

	REQUIRE(md.EventsOf(circle).size() == 1);
	auto& event = md.Events()[md.EventsOf(circle)[0] - 1];
	CHECK(event.Name == L"Changed");
	CHECK(event.Adder == FindMethodNamed(md, circle, L"add_Changed"));
	CHECK(event.Remover == FindMethodNamed(md, circle, L"remove_Changed"));
	CHECK(md.EventDeclaration(md.EventsOf(circle)[0]) == L".event [mscorlib]System.EventHandler Changed");

	// [Note("a shape", Color.Blue, Priority = 3, Related = typeof(IShape))]: an enum of this file, named arguments, a type
	// (and the compiler's [DefaultMember("Item")], for the indexer)
	auto attributes = md.CustomAttributes((ClrTableId::TypeDef << 24) | circle);
	REQUIRE(attributes.size() == 2);
	auto& note = md.CSharpType(attributes[0].AttributeType).Name == L"NoteAttribute" ? attributes[0] : attributes[1];
	auto& member = &note == &attributes[0] ? attributes[1] : attributes[0];
	CHECK(md.AttributeCSharp(member) == L"DefaultMember(\"Item\")");
	CHECK(note.Decoded);
	CHECK(md.AttributeCSharp(note) == L"Note(\"a shape\", Color.Blue, Priority = 3, Related = typeof(IShape))");
	CHECK(md.AttributeIl(note) == L".custom instance void Sample.NoteAttribute::.ctor(string, valuetype Sample.Color) = "
		L"{ string('a shape') int32(6) field int32 'Priority' = int32(3) field type 'Related' = type(Sample.IShape) }");
	// [AttributeUsage(AttributeTargets.Class | AttributeTargets.Method, AllowMultiple = true)]: an enum of another assembly
	auto usage = md.CustomAttributes((ClrTableId::TypeDef << 24) | FindTypeNamed(md, L"NoteAttribute"));
	REQUIRE(usage.size() == 1);
	CHECK(md.AttributeCSharp(usage[0]) == L"AttributeUsage(AttributeTargets.Class | AttributeTargets.Method, AllowMultiple = true)");

	auto beep = FindMethodNamed(md, FindTypeNamed(md, L"Algorithms"), L"Beep");
	auto native = md.PInvoke(beep);
	REQUIRE(native);
	CHECK(native->Module == L"kernel32.dll");
	CHECK(native->EntryPoint == L"Beep");
	CHECK((native->Flags & 0x40) != 0);	// SetLastError
	CHECK_FALSE(md.PInvoke(FindMethodNamed(md, FindTypeNamed(md, L"Algorithms"), L"Sum")));
}

TEST_CASE("The IL of a type has its properties, events and custom attributes", "[clr][il]") {
	auto path = CompiledSample(true);
	if (path.empty())
		SKIP("no C# compiler of the .NET Framework");
	PEFile pe;
	REQUIRE(pe.Open(path));
	ClrMetadata md;
	REQUIRE(md.Parse(pe));
	auto il = DisassembleIlType(pe, md, FindTypeNamed(md, L"Circle"));
	CHECK_THAT(Narrow(il), Catch::Matchers::ContainsSubstring(".custom instance void Sample.NoteAttribute::.ctor(string, valuetype Sample.Color)"));
	CHECK_THAT(Narrow(il), Catch::Matchers::ContainsSubstring(".property instance float64 Radius()"));
	CHECK_THAT(Narrow(il), Catch::Matchers::ContainsSubstring(".get instance float64 Sample.Circle::get_Radius()"));
	CHECK_THAT(Narrow(il), Catch::Matchers::ContainsSubstring(".set instance void Sample.Circle::set_Radius(float64)"));
	CHECK_THAT(Narrow(il), Catch::Matchers::ContainsSubstring(".event [mscorlib]System.EventHandler Changed"));
	CHECK_THAT(Narrow(il), Catch::Matchers::ContainsSubstring(".addon instance void Sample.Circle::add_Changed(class [mscorlib]System.EventHandler)"));
	auto beep = DisassembleIlMethod(pe, md, FindMethodNamed(md, FindTypeNamed(md, L"Algorithms"), L"Beep"));
	CHECK_THAT(Narrow(beep), Catch::Matchers::ContainsSubstring("pinvokeimpl(\"kernel32.dll\" lasterr winapi)"));
	auto property = DisassembleIlProperty(pe, md, md.PropertiesOf(FindTypeNamed(md, L"Circle"))[0]);
	CHECK_THAT(Narrow(property), Catch::Matchers::StartsWith(".property instance float64 Radius()"));
	CHECK_THAT(Narrow(property), Catch::Matchers::ContainsSubstring("ldfld      float64 Sample.Circle::_radius"));
}

TEST_CASE("C# is decompiled as it was written", "[clr][cs]") {
	// the compiler writes other IL when it does not optimize, the C# is the same
	auto optimize = GENERATE(true, false);
	auto path = CompiledSample(optimize);
	if (path.empty())
		SKIP("no C# compiler of the .NET Framework");
	INFO((optimize ? "optimized" : "not optimized"));
	PEFile pe;
	REQUIRE(pe.Open(path));
	ClrMetadata md;
	REQUIRE(md.Parse(pe));
	auto algorithms = FindTypeNamed(md, L"Algorithms");
	auto circle = FindTypeNamed(md, L"Circle");
	auto method = [&](uint32_t type, std::wstring_view name) {
		auto text = Narrow(DecompileClrMethod(pe, md, FindMethodNamed(md, type, name)));
		UNSCOPED_INFO(text);
		return text;
	};
	using Catch::Matchers::ContainsSubstring;

	auto sum = method(algorithms, L"Sum");
	CHECK_THAT(sum, ContainsSubstring("public static int Sum(int[] values)"));
	CHECK_THAT(sum, ContainsSubstring("for (int num2 = 0; num2 < values.Length; num2++)"));
	CHECK_THAT(sum, ContainsSubstring("num += values[num2];"));

	auto classify = method(algorithms, L"Classify");
	CHECK_THAT(classify, ContainsSubstring("switch (n)"));
	CHECK_THAT(classify, ContainsSubstring("case 3:"));
	CHECK_THAT(classify, ContainsSubstring("default:"));

	auto describe = method(algorithms, L"Describe");
	CHECK_THAT(describe, ContainsSubstring("string text = x > 0 && verbose ? \"positive\" : \"other\";"));
	CHECK_THAT(describe, ContainsSubstring("if (x < 0 || x > 100)"));
	CHECK_THAT(describe, ContainsSubstring("else if (x == 50)"));
	CHECK_THAT(describe, ContainsSubstring("text = text.ToUpper();"));

	auto count = method(algorithms, L"Count");
	CHECK_THAT(count, ContainsSubstring("for (int num2 = 0; num2 < text.Length; num2++)"));
	CHECK_THAT(count, ContainsSubstring("if (text[num2] == c)"));
	CHECK_THAT(count, ContainsSubstring("num++;"));

	CHECK_THAT(method(algorithms, L"Repeat"), ContainsSubstring("} while (n > 0);"));

	// the values of enums by name, those of other assemblies as well
	CHECK_THAT(method(algorithms, L"Open"), ContainsSubstring("return File.Open(path, FileMode.OpenOrCreate, FileAccess.Read);"));
	CHECK_THAT(method(algorithms, L"Same"), ContainsSubstring("return string.Equals(a, b, StringComparison.OrdinalIgnoreCase);"));
	auto mode = method(algorithms, L"Mode");
	CHECK_THAT(mode, ContainsSubstring("if ((attributes & FileAttributes.Hidden) != 0)"));
	CHECK_THAT(mode, ContainsSubstring("if (access == (Access.Read | Access.Write))"));
	CHECK_THAT(mode, ContainsSubstring("if ((access & ~Access.Write) == Access.Read)"));
	CHECK_THAT(mode, ContainsSubstring("FileAttributes.Normal"));

	auto guarded = method(algorithms, L"Guarded");
	CHECK_THAT(guarded, ContainsSubstring("throw new InvalidOperationException(\"too long\");"));
	CHECK_THAT(guarded, ContainsSubstring("catch (InvalidOperationException ex)"));
	CHECK_THAT(guarded, ContainsSubstring("text = ex.Message;"));
	CHECK_THAT(guarded, ContainsSubstring("catch (Exception)"));
	CHECK_THAT(guarded, ContainsSubstring("finally"));
	CHECK(guarded.find("try") == guarded.rfind("try"));	// one try, with its catches and its finally

	CHECK_THAT(method(algorithms, L"IsEmpty"), ContainsSubstring("return list != null && list.Count == 0;"));

	auto fill = method(algorithms, L"Fill");
	CHECK_THAT(fill, ContainsSubstring("foreach (string text in keys)"));
	CHECK_THAT(fill, ContainsSubstring("if (!map.ContainsKey(text))"));
	CHECK_THAT(fill, ContainsSubstring("map[text] = text.Length;"));

	CHECK_THAT(method(algorithms, L"Locked"), ContainsSubstring("lock (gate)"));
	CHECK_THAT(method(algorithms, L"Length"), ContainsSubstring("using (FileStream fileStream = File.OpenRead(path))"));
	auto beep = method(algorithms, L"Beep");
	CHECK_THAT(beep, ContainsSubstring("[DllImport(\"kernel32.dll\", SetLastError = true)]"));
	CHECK_THAT(beep, ContainsSubstring("public static extern bool Beep(uint frequency, uint duration);"));

	CHECK_THAT(method(circle, L"OnChanged"), ContainsSubstring("eventHandler(this, EventArgs.Empty);"));
	CHECK_THAT(method(circle, L"ToString"), ContainsSubstring("return \"Circle \" + _radius;"));
	CHECK_THAT(method(circle, L"op_Addition"), ContainsSubstring("public static Circle operator +(Circle a, Circle b)"));
	CHECK_THAT(method(circle, L"op_Addition"), ContainsSubstring("return new Circle(a._radius + b._radius);"));

	// a type: its attributes, members and accessors
	auto type = Narrow(DecompileClrType(pe, md, circle));
	UNSCOPED_INFO(type);
	CHECK_THAT(type, ContainsSubstring("namespace Sample"));
	CHECK_THAT(type, ContainsSubstring("[Serializable]"));
	CHECK_THAT(type, ContainsSubstring("[Note(\"a shape\", Color.Blue, Priority = 3, Related = typeof(IShape))]"));
	CHECK_THAT(type, ContainsSubstring("public class Circle : IShape"));
	CHECK_THAT(type, ContainsSubstring("public const int Sides = 0;"));
	CHECK_THAT(type, ContainsSubstring("public event EventHandler Changed;"));
	CHECK_THAT(type, ContainsSubstring("public double Radius"));
	CHECK_THAT(type, ContainsSubstring("if (value < 0.0)"));
	CHECK_THAT(type, ContainsSubstring("public int this[int index]"));
	CHECK_THAT(type, ContainsSubstring("public Circle(double radius)"));
	CHECK_THAT(type, ContainsSubstring("protected virtual void OnChanged()"));
	CHECK_THAT(type, ContainsSubstring("public override string ToString()"));
	CHECK(type.find("get_Radius") == std::string::npos);	// the accessors are in the property

	auto note = Narrow(DecompileClrType(pe, md, FindTypeNamed(md, L"NoteAttribute")));
	UNSCOPED_INFO(note);
	CHECK_THAT(note, ContainsSubstring("public sealed class NoteAttribute : Attribute"));
	CHECK_THAT(note, ContainsSubstring("public string Text { get; private set; }"));
	CHECK_THAT(note, ContainsSubstring("public Color Color { get; set; }"));

	auto shape = Narrow(DecompileClrType(pe, md, FindTypeNamed(md, L"IShape")));
	CHECK_THAT(shape, ContainsSubstring("public interface IShape"));
	CHECK_THAT(shape, ContainsSubstring("double Area { get; }"));
	CHECK_THAT(shape, ContainsSubstring("event EventHandler Changed;"));

	auto color = Narrow(DecompileClrType(pe, md, FindTypeNamed(md, L"Color")));
	CHECK_THAT(color, ContainsSubstring("Green = 5,"));
	CHECK_THAT(Narrow(DecompileClrType(pe, md, FindTypeNamed(md, L"Access"))), ContainsSubstring("public enum Access : byte"));
	CHECK(CSharpTypeDeclaration(md, circle) == L"public class Circle : IShape");

	// nothing of the sample needs goto
	for (uint32_t t = 1; t <= md.Types().size(); t++)
		CHECK(DecompileClrType(pe, md, t).find(L"goto") == std::wstring::npos);
}

TEST_CASE("Damaged IL is decompiled without failing", "[clr][cs]") {
	auto path = CompiledSample(true);
	if (path.empty())
		SKIP("no C# compiler of the .NET Framework");
	std::vector<uint8_t> bytes;
	{
		std::ifstream in(path, std::ios::binary);
		bytes.assign(std::istreambuf_iterator<char>(in), {});
	}
	PEFile original;
	REQUIRE(original.Open(path));
	ClrMetadata md0;
	REQUIRE(md0.Parse(original));
	// the code of the methods: its bytes are changed at random, again and again
	std::vector<std::pair<uint32_t, uint32_t>> code;
	for (auto& m : md0.Methods()) {
		IlMethodBody body;
		if (m.Rva && ReadIlMethodBody(original, m.Rva, body))
			code.emplace_back((uint32_t)original.GetOffsetFromRVA(m.Rva + body.HeaderSize), (uint32_t)body.Code.size());
	}
	REQUIRE(!code.empty());
	std::mt19937 random(12345);
	for (int round = 0; round < 200; round++) {
		auto damaged = bytes;
		for (int i = 0; i < 8; i++) {
			auto& [offset, size] = code[random() % code.size()];
			damaged[offset + random() % size] = (uint8_t)random();
		}
		auto file = path + L".damaged.dll";
		{
			std::ofstream out(file, std::ios::binary);
			out.write((char const*)damaged.data(), damaged.size());
		}
		PEFile pe;
		REQUIRE(pe.Open(file));
		ClrMetadata md;
		REQUIRE(md.Parse(pe));
		for (uint32_t t = 1; t <= md.Types().size(); t++)
			CHECK_NOTHROW(DecompileClrType(pe, md, t));
		pe.Close();
	}
}
