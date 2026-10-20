#include "pch.h"
#include <PEFile.h>
#include <PEReport.h>
#include <LIEF/logging.hpp>
#include "Inputs.h"
#include "Output.h"

//
// tpe: the analyses of TotalPE from the command line, as JSON for tools or as text for people.
//
//   tpe info <file>...          a report of each file
//   tpe scan <path>...          a report of each PE file of directories, one line of JSON each (JSONL), and/or a CSV summary
//   tpe diff <old> <new>        the differences between two files, or between the files of two directories
//   tpe check <path>...         whether files have the protections they must have (for builds and audits)
//

namespace {
	constexpr char ToolName[] = "tpe";
	constexpr char ToolVersion[] = "3.0.0";

	enum ExitCode {
		Success = 0,
		SomeFailed = 1,		// some files could not be read or are not PE files (diff: the files differ)
		UsageError = 2,		// (diff: a file could not be read)
		CheckFailed = 3,	// check: some files do not meet the requirements
	};

	std::atomic_bool g_Cancel;

	const char Usage[] =
R"(tpe - TotalPE from the command line

Usage:
  tpe info <file>... [options]       a report of each file (JSON, or text with --text)
  tpe scan <path>... [options]       reports of the PE files of directories, files and wildcards
  tpe diff <old> <new> [options]     the differences between two files or two directories
  tpe check <path>... [options]      whether files have the protections they must have

Report options (info, scan):
  --parts <list>      parts to include, separated by commas (default: "default"):
                      file headers sections imports exports debug rich tls version
                      signature security overlay anomalies rtti, or all / default
  --all               all the parts (--parts all)
  --trust             ask Windows whether signatures are valid and trusted
  --no-resolve        do not name imports by ordinal from the DLLs of this system
  --no-catalog        do not look for the system catalogs that sign files without
                      a signature of their own (most files of Windows)
  --max-exports <n>   list at most n exports of a file

Output:
  --text              text for people instead of JSON (info, diff)
  --json              JSON lines instead of text (check)
  --compact           JSON on one line (info, diff of two files)
  -o <file>           write to a file instead of the standard output (info, diff, check)

Searching (scan, check, diff of directories):
  -r, --recursive     search subdirectories
  --include <specs>   file patterns for directories, separated by ';'
                      (default: *.exe;*.dll;*.sys;*.ocx;*.cpl;*.scr;*.drv;*.efi;...)
  -j <n>              files to analyze at once (default: the number of processors)
  -q, --quiet         no progress or summary on the standard error
  Files that do not start with "MZ" are skipped (and counted).

scan:
  --jsonl <file>      write the reports to a file, one JSON object per line
  --csv <file>        write a summary row for each file (CSV)
  Without --jsonl or --csv, the reports go to the standard output as JSONL.

diff:
  --same              list the items that are the same too
  Two directories are compared file by file (by relative path): one line of JSON
  for each, or with --text the files that differ.
  Exit codes: 0 no differences, 1 differences, 2 trouble (as diff does).

check:
  --require <list>    what each file must have, separated by commas (default: default):
                      aslr high-entropy-aslr dep cfg xfg cet ehcont gs safeseh no-wx
                      retpoline force-integrity signed trusted, or default
                      (default = aslr,dep,cfg,gs,no-wx; trusted implies --trust)
  --fail-on <level>   anomalies of this severity or worse fail: info, warning, error
  -v, --verbose       list the files that pass too
  Exit code 3 if a file fails.

Exit codes: 0 success, 1 some files failed, 2 bad arguments, 3 a check failed
)";

	struct Options {
		std::wstring Command;
		std::vector<std::wstring> Inputs;
		ReportOptions Report;
		bool Text{ false }, Json{ false }, Compact{ false }, Quiet{ false }, Verbose{ false }, Same{ false };
		std::wstring OutputPath, JsonlPath, CsvPath;
		InputOptions Search;
		unsigned Threads{ 0 };
		std::vector<std::string> Require;
		std::string FailOn;
	};

	void Error(std::string_view message) {
		Output::StdErr().WriteLine(std::format("{}: {}", ToolName, message));
	}

	bool ParseNumber(std::wstring const& text, size_t& value) {
		wchar_t* end;
		auto n = wcstoull(text.c_str(), &end, 10);
		if (text.empty() || *end)
			return false;
		value = (size_t)n;
		return true;
	}

	bool ParseArguments(int argc, wchar_t* argv[], Options& options) {
		if (argc < 2)
			return false;
		options.Command = argv[1];
		for (int i = 2; i < argc; i++) {
			std::wstring arg = argv[i];
			auto value = [&](std::wstring& result) {
				if (i + 1 >= argc) {
					Error(std::format("{} needs a value", ToUtf8(arg)));
					return false;
				}
				result = argv[++i];
				return true;
			};
			std::wstring v;
			if (arg == L"--parts") {
				if (!value(v))
					return false;
				std::string unknown;
				if (!ParseReportParts(ToUtf8(v), options.Report.Parts, &unknown)) {
					Error(std::format("unknown part: {}", unknown));
					return false;
				}
			}
			else if (arg == L"--all")
				options.Report.Parts = ReportPart::All;
			else if (arg == L"--trust")
				options.Report.VerifyTrust = true;
			else if (arg == L"--no-resolve")
				options.Report.ResolveOrdinals = false;
			else if (arg == L"--no-catalog")
				options.Report.CheckCatalogs = false;
			else if (arg == L"--max-exports") {
				if (!value(v) || !ParseNumber(v, options.Report.MaxExports)) {
					Error("--max-exports needs a number");
					return false;
				}
			}
			else if (arg == L"--text")
				options.Text = true;
			else if (arg == L"--json")
				options.Json = true;
			else if (arg == L"--compact")
				options.Compact = true;
			else if (arg == L"-o" || arg == L"--output") {
				if (!value(options.OutputPath))
					return false;
			}
			else if (arg == L"-r" || arg == L"--recursive")
				options.Search.Recursive = true;
			else if (arg == L"--include") {
				if (!value(options.Search.Include))
					return false;
			}
			else if (arg == L"-j") {
				size_t n;
				if (!value(v) || !ParseNumber(v, n) || n == 0 || n > 256) {
					Error("-j needs a number from 1 to 256");
					return false;
				}
				options.Threads = (unsigned)n;
			}
			else if (arg == L"--jsonl") {
				if (!value(options.JsonlPath))
					return false;
			}
			else if (arg == L"--csv") {
				if (!value(options.CsvPath))
					return false;
			}
			else if (arg == L"-q" || arg == L"--quiet")
				options.Quiet = true;
			else if (arg == L"-v" || arg == L"--verbose")
				options.Verbose = true;
			else if (arg == L"--same")
				options.Same = true;
			else if (arg == L"--require") {
				if (!value(v))
					return false;
				std::string unknown;
				if (!ParseRequirements(ToUtf8(v), options.Require, &unknown)) {
					Error(std::format("unknown requirement: {}", unknown));
					return false;
				}
			}
			else if (arg == L"--fail-on") {
				if (!value(v))
					return false;
				options.FailOn = ToUtf8(v);
				if (options.FailOn != "info" && options.FailOn != "warning" && options.FailOn != "error") {
					Error("--fail-on needs info, warning or error");
					return false;
				}
			}
			else if (arg.starts_with(L"-") && arg.size() > 1) {
				Error(std::format("unknown option: {}", ToUtf8(arg)));
				return false;
			}
			else
				options.Inputs.push_back(arg);
		}
		return true;
	}

	std::string SystemMessage(DWORD error) {
		wchar_t* text = nullptr;
		::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, (PWSTR)&text, 0, nullptr);
		std::wstring message = text ? text : std::format(L"error {}", error);
		::LocalFree(text);
		while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r' || message.back() == L'.'))
			message.pop_back();
		return ToUtf8(message);
	}

	JsonValue WithTool(JsonValue const& report) {
		auto object = report.AsObject();
		object["tool"] = JsonValue::Object{ { "name", ToolName }, { "version", ToolVersion } };
		return object;
	}

	// The file of an output option, or the standard output; null (after an error message) if the file cannot be created
	Output* OpenOutput(std::wstring const& path, Output& file) {
		if (path.empty())
			return &Output::StdOut();
		if (!file.Create(path)) {
			Error(std::format("cannot create {}: {}", ToUtf8(path), SystemMessage(::GetLastError())));
			return nullptr;
		}
		return &file;
	}

	enum class FileStatus {
		Ok,
		NotPE,		// does not start with "MZ"
		Failed,		// cannot be read or parsed
	};

	// A quick look before parsing: the size (PEFile handles up to 4 GB) and the signature, so that other files are not parsed
	FileStatus Probe(std::wstring const& path, std::string& error) {
		wil::unique_hfile file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
		if (!file) {
			error = SystemMessage(::GetLastError());
			return FileStatus::Failed;
		}
		LARGE_INTEGER size;
		if (!::GetFileSizeEx(file.get(), &size)) {
			error = SystemMessage(::GetLastError());
			return FileStatus::Failed;
		}
		if (size.QuadPart > MAXDWORD) {
			error = "the file is larger than 4 GB";
			return FileStatus::Failed;
		}
		char mz[2]{};
		DWORD read = 0;
		if (!::ReadFile(file.get(), mz, 2, &read, nullptr) || read < 2 || mz[0] != 'M' || mz[1] != 'Z') {
			error = "not a PE file";
			return FileStatus::NotPE;
		}
		return FileStatus::Ok;
	}

	// Opens a PE file; false with the reason if it cannot be
	FileStatus OpenPE(std::wstring const& path, PEFile& pe, std::string& error) {
		auto status = Probe(path, error);
		if (status != FileStatus::Ok)
			return status;
		try {
			if (!pe.Open(path)) {
				error = "not a valid PE file";
				return FileStatus::Failed;
			}
		}
		catch (std::exception const& ex) {
			error = std::format("the file could not be read: {}", ex.what());
			return FileStatus::Failed;
		}
		return FileStatus::Ok;
	}

	struct FileResult {
		FileStatus Status{};
		JsonValue Report;
	};

	FileResult AnalyzeFile(std::wstring const& path, ReportOptions const& options) {
		auto fail = [&](FileStatus status, std::string const& error) {
			return FileResult{ status, WithTool(BuildErrorReport(path, error)) };
		};
		PEFile pe;
		std::string error;
		if (auto status = OpenPE(path, pe, error); status != FileStatus::Ok)
			return fail(status, error);
		try {
			auto report = BuildReport(pe, options);
			return FileResult{ report["error"].IsString() ? FileStatus::Failed : FileStatus::Ok, WithTool(report) };
		}
		catch (std::exception const& ex) {
			return fail(FileStatus::Failed, std::format("the file could not be analyzed: {}", ex.what()));
		}
		catch (...) {
			return fail(FileStatus::Failed, "the file could not be analyzed");
		}
	}

	std::vector<std::wstring> Expand(Options const& options, bool& problems) {
		std::vector<std::wstring> messages;
		auto files = ExpandInputs(options.Inputs, options.Search, messages);
		for (auto const& m : messages)
			Error(ToUtf8(m));
		problems = !messages.empty();
		return files;
	}

	// Does 'work' for items 0..count-1 on worker threads, and hands each result to 'consume' on this thread in the order of
	// the items, so that the output does not depend on the threads. Shows progress (the label of an item) on a console.
	// False if cancelled (Ctrl+C).
	template<typename Result>
	bool RunOrdered(size_t count, Options const& options, std::function<Result(size_t)> const& work,
		std::function<void(size_t, Result&)> const& consume, std::function<std::wstring(size_t)> const& label) {
		std::vector<std::optional<Result>> slots(count);
		std::mutex lock;
		std::condition_variable ready;
		std::atomic<size_t> next{ 0 };

		auto threads = options.Threads ? options.Threads : std::max(1u, std::thread::hardware_concurrency());
		threads = (unsigned)std::min<size_t>(threads, std::max<size_t>(count, 1));
		std::vector<std::jthread> workers;
		for (unsigned t = 0; t < threads; t++)
			workers.emplace_back([&] {
				for (size_t i; !g_Cancel && (i = next++) < count; ) {
					auto result = work(i);
					std::lock_guard guard(lock);
					slots[i] = std::move(result);
					ready.notify_all();
				}
				// when cancelled, the item waited for may never be done
				std::lock_guard guard(lock);
				ready.notify_all();
			});

		auto& err = Output::StdErr();
		const bool progress = !options.Quiet && err.IsConsole();
		auto lastProgress = ::GetTickCount64();
		for (size_t i = 0; i < count; i++) {
			Result result;
			{
				std::unique_lock guard(lock);
				ready.wait(guard, [&] { return slots[i].has_value() || g_Cancel; });
				if (!slots[i])
					break;
				result = std::move(*slots[i]);
				slots[i].reset();
			}
			consume(i, result);
			if (progress && ::GetTickCount64() - lastProgress > 200) {
				lastProgress = ::GetTickCount64();
				auto name = label(i);
				if (name.size() > 50)
					name = name.substr(0, 47) + L"...";
				err.Write(std::format("\r[{}/{}] {:<50}", i + 1, count, ToUtf8(name)));
			}
		}
		workers.clear();
		if (progress)
			err.Write(std::format("\r{:<70}\r", ""));
		return !g_Cancel;
	}

	std::wstring FileName(std::wstring const& path) {
		return std::filesystem::path(path).filename().wstring();
	}

	//
	// info
	//

	int Info(Options const& options) {
		if (options.Inputs.empty()) {
			Error("info needs at least one file");
			return UsageError;
		}
		Output file;
		auto out = OpenOutput(options.OutputPath, file);
		if (!out)
			return UsageError;
		bool problems;
		auto files = Expand(options, problems);

		bool failed = problems;
		JsonValue::Array reports;
		for (auto const& path : files) {
			if (g_Cancel)
				break;
			auto result = AnalyzeFile(path, options.Report);
			failed |= result.Status != FileStatus::Ok;
			if (options.Text) {
				if (!reports.empty())
					out->Write("\n");
				out->Write(FormatReportText(result.Report));
			}
			reports.push_back(std::move(result.Report));
		}
		if (!options.Text && !reports.empty())
			out->WriteLine(reports.size() == 1 ? reports[0].Write(!options.Compact) : JsonValue(std::move(reports)).Write(!options.Compact));
		return failed ? SomeFailed : Success;
	}

	//
	// scan
	//

	int Scan(Options const& options) {
		if (options.Inputs.empty()) {
			Error("scan needs at least one directory, file or wildcard");
			return UsageError;
		}
		Output jsonlFile, csvFile;
		Output* jsonl = nullptr;
		Output* csv = nullptr;
		if (!options.JsonlPath.empty() || options.CsvPath.empty())
			if (!(jsonl = OpenOutput(options.JsonlPath, jsonlFile)))
				return UsageError;
		if (!options.CsvPath.empty() && !(csv = OpenOutput(options.CsvPath, csvFile)))
			return UsageError;

		bool problems;
		auto files = Expand(options, problems);
		auto& columns = ReportSummaryColumns();
		if (csv) {
			csv->Write("\xEF\xBB\xBF");		// so that Excel reads it as UTF-8
			csv->WriteLine(CsvRow(columns));
		}

		size_t ok = 0, failed = 0, skipped = 0;
		bool complete = RunOrdered<FileResult>(files.size(), options,
			[&](size_t i) { return AnalyzeFile(files[i], options.Report); },
			[&](size_t, FileResult& result) {
				if (result.Status == FileStatus::NotPE) {
					skipped++;
					return;
				}
				(result.Status == FileStatus::Ok ? ok : failed)++;
				if (jsonl)
					jsonl->WriteLine(result.Report.Write(false));
				if (csv)
					csv->WriteLine(CsvRow(CsvValues(SummarizeReport(result.Report), columns)));
			},
			[&](size_t i) { return FileName(files[i]); });

		if (!options.Quiet)
			Output::StdErr().WriteLine(std::format("{} files: {} analyzed, {} failed, {} skipped (not PE){}", files.size(), ok, failed,
				skipped, complete ? "" : ", cancelled"));
		return failed || problems || !complete ? SomeFailed : Success;
	}

	//
	// check
	//

	int Check(Options const& options) {
		if (options.Inputs.empty()) {
			Error("check needs at least one directory, file or wildcard");
			return UsageError;
		}
		Output file;
		auto out = OpenOutput(options.OutputPath, file);
		if (!out)
			return UsageError;

		auto requirements = options.Require;
		if (requirements.empty())
			ParseRequirements(DefaultRequirements, requirements);
		// only the parts that the requirements are about
		auto report = options.Report;
		report.Parts = ReportPart::Signature | ReportPart::Security | ReportPart::Anomalies;
		if (std::ranges::find(requirements, "trusted") != requirements.end())
			report.VerifyTrust = true;

		bool problems;
		auto files = Expand(options, problems);
		size_t passed = 0, failed = 0, errors = 0, skipped = 0;
		bool complete = RunOrdered<FileResult>(files.size(), options,
			[&](size_t i) { return AnalyzeFile(files[i], report); },
			[&](size_t i, FileResult& result) {
				if (result.Status == FileStatus::NotPE) {
					skipped++;
					return;
				}
				auto failures = CheckReport(result.Report, requirements, options.FailOn);
				auto path = ToUtf8(files[i]);
				if (result.Status != FileStatus::Ok)
					errors++;
				else
					(failures.empty() ? passed : failed)++;

				if (options.Json) {
					JsonValue::Array list;
					for (auto const& f : failures)
						list.emplace_back(f);
					JsonValue::Object line{ { "path", path }, { "pass", failures.empty() }, { "failures", std::move(list) } };
					if (result.Status != FileStatus::Ok)
						line["error"] = result.Report["error"];
					out->WriteLine(JsonValue(std::move(line)).Write(false));
				}
				else if (!failures.empty()) {
					out->WriteLine(std::format("{} {}", result.Status == FileStatus::Ok ? "FAIL" : "ERROR", path));
					for (auto const& f : failures)
						out->WriteLine("  " + f);
				}
				else if (options.Verbose)
					out->WriteLine("PASS " + path);
			},
			[&](size_t i) { return FileName(files[i]); });

		if (!options.Quiet) {
			std::string required;
			for (auto const& r : requirements)
				required += (required.empty() ? "" : ",") + r;
			Output::StdErr().WriteLine(std::format("{} files: {} passed, {} failed, {} not analyzed, {} skipped (not PE){} [require {}{}]",
				files.size(), passed, failed, errors, skipped, complete ? "" : ", cancelled", required,
				options.FailOn.empty() ? "" : "; fail on " + options.FailOn));
		}
		if (failed)
			return CheckFailed;
		return errors || problems || !complete ? SomeFailed : Success;
	}

	//
	// diff
	//

	enum class PairStatus { Identical, Changed, OneSided, Error, Skipped };

	struct DiffResult {
		PairStatus Status{};
		JsonValue Report;
	};

	DiffResult DiffFiles(std::wstring const& left, std::wstring const& right, bool includeSame) {
		PEFile a, b;
		std::string errorA, errorB;
		auto statusA = OpenPE(left, a, errorA), statusB = OpenPE(right, b, errorB);
		if (statusA == FileStatus::NotPE && statusB == FileStatus::NotPE)
			return { PairStatus::Skipped };
		if (statusA != FileStatus::Ok || statusB != FileStatus::Ok) {
			auto error = statusA != FileStatus::Ok ? std::format("{}: {}", ToUtf8(left), errorA) : std::format("{}: {}", ToUtf8(right), errorB);
			JsonValue::Object report{
				{ "schema", DiffSchema },
				{ "left", JsonValue::Object{ { "path", ToUtf8(left) } } },
				{ "right", JsonValue::Object{ { "path", ToUtf8(right) } } },
				{ "status", "error" },
				{ "error", error },
			};
			return { PairStatus::Error, WithTool(report) };
		}
		try {
			auto report = BuildDiffReport(a, b, includeSame);
			return { report["status"].AsString() == "identical" ? PairStatus::Identical : PairStatus::Changed, WithTool(report) };
		}
		catch (std::exception const& ex) {
			JsonValue::Object report{ { "schema", DiffSchema }, { "status", "error" }, { "error", std::format("the files could not be compared: {}", ex.what()) } };
			return { PairStatus::Error, WithTool(report) };
		}
	}

	int DiffTwoFiles(Options const& options, Output& out) {
		auto result = DiffFiles(options.Inputs[0], options.Inputs[1], options.Same);
		if (result.Status == PairStatus::Skipped) {
			Error("neither file is a PE file");
			return UsageError;
		}
		if (options.Text)
			out.Write(FormatDiffText(result.Report));
		else
			out.WriteLine(result.Report.Write(!options.Compact));
		return result.Status == PairStatus::Error ? UsageError : result.Status == PairStatus::Identical ? Success : SomeFailed;
	}

	int DiffDirectories(Options const& options, Output& out) {
		std::vector<std::wstring> problems;
		std::filesystem::path leftDir(options.Inputs[0]), rightDir(options.Inputs[1]);
		auto left = ListDirectory(leftDir, options.Search, problems), right = ListDirectory(rightDir, options.Search, problems);
		for (auto const& p : problems)
			Error(ToUtf8(p));

		// the relative paths of both, matched without regard to case (as Windows does)
		struct Pair {
			std::wstring Relative;
			bool Left{ false }, Right{ false };
		};
		std::vector<Pair> pairs;
		size_t l = 0, r = 0;
		while (l < left.size() || r < right.size()) {
			int cmp = l == left.size() ? 1 : r == right.size() ? -1 : _wcsicmp(left[l].c_str(), right[r].c_str());
			if (cmp == 0)
				pairs.push_back({ left[l++], true, true }), r++;
			else if (cmp < 0)
				pairs.push_back({ left[l++], true, false });
			else
				pairs.push_back({ right[r++], false, true });
		}

		size_t counts[5]{};
		bool complete = RunOrdered<DiffResult>(pairs.size(), options,
			[&](size_t i) {
				auto const& p = pairs[i];
				auto leftPath = (leftDir / p.Relative).wstring(), rightPath = (rightDir / p.Relative).wstring();
				if (p.Left && p.Right)
					return DiffFiles(leftPath, rightPath, options.Same);
				return DiffResult{ PairStatus::OneSided, WithTool(BuildOneSidedDiffReport(p.Right ? rightPath : leftPath, p.Right)) };
			},
			[&](size_t i, DiffResult& result) {
				counts[(int)result.Status]++;
				if (result.Status == PairStatus::Skipped)
					return;
				if (!options.Text)
					out.WriteLine(result.Report.Write(false));
				else if (result.Status != PairStatus::Identical)
					out.Write(FormatDiffText(result.Report) + "\n");
			},
			[&](size_t i) { return pairs[i].Relative; });

		size_t added = 0, removed = 0;
		for (auto const& p : pairs)
			added += p.Right && !p.Left, removed += p.Left && !p.Right;
		if (!options.Quiet)
			Output::StdErr().WriteLine(std::format("{} files: {} identical, {} changed, {} added, {} removed, {} errors, {} skipped (not PE){}",
				pairs.size(), counts[(int)PairStatus::Identical], counts[(int)PairStatus::Changed], added, removed,
				counts[(int)PairStatus::Error], counts[(int)PairStatus::Skipped], complete ? "" : ", cancelled"));
		if (counts[(int)PairStatus::Error] || !problems.empty() || !complete)
			return UsageError;
		return counts[(int)PairStatus::Changed] || counts[(int)PairStatus::OneSided] ? SomeFailed : Success;
	}

	int Diff(Options const& options) {
		if (options.Inputs.size() != 2) {
			Error("diff needs two files or two directories: the old and the new");
			return UsageError;
		}
		Output file;
		auto out = OpenOutput(options.OutputPath, file);
		if (!out)
			return UsageError;
		std::error_code ec;
		bool leftDir = std::filesystem::is_directory(options.Inputs[0], ec), rightDir = std::filesystem::is_directory(options.Inputs[1], ec);
		if (leftDir != rightDir) {
			Error("diff compares two files or two directories, not a file with a directory");
			return UsageError;
		}
		return leftDir ? DiffDirectories(options, *out) : DiffTwoFiles(options, *out);
	}
}

int wmain(int argc, wchar_t* argv[]) {
	// LIEF tells about every damaged structure on the standard error, which a scan does not want
	LIEF::logging::disable();
	::SetConsoleCtrlHandler([](DWORD type) -> BOOL {
		if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT)
			return FALSE;
		g_Cancel = true;
		return TRUE;
	}, TRUE);

	if (argc >= 2 && (wcscmp(argv[1], L"--version") == 0 || wcscmp(argv[1], L"version") == 0)) {
		Output::StdOut().WriteLine(std::format("{} {} (schemas {}, {})", ToolName, ToolVersion, ReportSchema, DiffSchema));
		return Success;
	}
	if (argc < 2 || wcscmp(argv[1], L"-h") == 0 || wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"help") == 0) {
		Output::StdOut().Write(Usage);
		return argc < 2 ? UsageError : Success;
	}
	Options options;
	if (!ParseArguments(argc, argv, options))
		return UsageError;

	if (options.Command == L"info")
		return Info(options);
	if (options.Command == L"scan")
		return Scan(options);
	if (options.Command == L"diff")
		return Diff(options);
	if (options.Command == L"check")
		return Check(options);
	Error(std::format("unknown command: {} (try tpe --help)", ToUtf8(options.Command)));
	return UsageError;
}
