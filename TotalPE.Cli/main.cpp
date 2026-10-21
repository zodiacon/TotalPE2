#include "pch.h"
#include <PEFile.h>
#include <PEReport.h>
#include <LIEF/logging.hpp>
#include "Inputs.h"
#include "Output.h"
#include <YaraRules.h>
#include <Capabilities.h>
#include <FuzzyHash.h>
#include <charconv>
#include <map>
#include <numeric>

//
// tpe: the analyses of TotalPE from the command line, as JSON for tools or as text for people.
//
//   tpe info <file>...          a report of each file
//   tpe scan <path>...          a report of each PE file of directories, one line of JSON each (JSONL), and/or a CSV summary
//   tpe diff <old> <new>        the differences between two files, or between the files of two directories
//   tpe check <path>...         whether files have the protections they must have (for builds and audits)
//   tpe cluster <path>...       groups of files that are alike (by fuzzy hashes or imphash): variants, builds of a program
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

	constexpr char ClusterSchema[] = "totalpe.clusters/1";

	enum class ClusterBy { Tlsh, Ssdeep, Imphash };
	constexpr int DefaultTlshThreshold = 30;
	constexpr int DefaultSsdeepThreshold = 50;

	const char Usage[] =
R"(tpe - TotalPE from the command line

Usage:
  tpe info <file>... [options]       a report of each file (JSON, or text with --text)
  tpe scan <path>... [options]       reports of the PE files of directories, files and wildcards
  tpe diff <old> <new> [options]     the differences between two files or two directories
  tpe check <path>... [options]      whether files have the protections they must have
  tpe cluster <path>... [options]    groups of files that are alike: variants, builds

Report options (info, scan):
  --parts <list>      parts to include, separated by commas (default: "default"):
                      file headers sections imports exports debug rich tls version
                      signature security overlay anomalies capabilities rtti,
                      or all / default (all but rtti)
  --all               all the parts (--parts all)
  --trust             ask Windows whether signatures are valid and trusted
  --no-resolve        do not name imports by ordinal from the DLLs of this system
  --no-catalog        do not look for the system catalogs that sign files without
                      a signature of their own (most files of Windows)
  --max-exports <n>   list at most n exports of a file
  --yara <path>       match YARA rules: a rule file (.yar, .yara), a directory of
                      them (subdirectories too), or compiled rules (.yarc). Rule files
                      with errors are left out (and listed). In check, a match fails.
  --yara-timeout <s>  seconds for the rules of a file (default 60, 0: no limit)
  --capabilities <path>  more rules of capabilities (JSON: a file, or a directory of
                      .json files), matched with those that come with tpe

Output:
  --text              text for people instead of JSON (info, diff, cluster)
  --json              JSON lines instead of text (check)
  --compact           JSON on one line (info, diff of two files, cluster)
  -o <file>           write to a file instead of the standard output (info, diff,
                      check, cluster)

Searching (scan, check, cluster, diff of directories):
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

cluster:
  --by <measure>      tlsh (default), ssdeep or imphash
  --threshold <n>     how close files must be to go together: a TLSH distance
                      of at most n (default 30), or an ssdeep score of at least n
                      (default 50)
  --csv <file>        a row for each file of a cluster
  -v, --verbose       list the files that are in no cluster too
  Each cluster is a center (*) and the files close to it, with their scores
  against it: the file close to the most others first, then the next of the files
  that are left. The output is JSON, or text with --text.

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
		std::wstring YaraPath;
		std::wstring CapabilitiesPath;
		ClusterBy ClusterBy{ ClusterBy::Tlsh };
		int Threshold{ -1 };	// -1: the default of the measure
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
			else if (arg == L"--yara") {
				if (!value(options.YaraPath))
					return false;
			}
			else if (arg == L"--capabilities") {
				if (!value(options.CapabilitiesPath))
					return false;
			}
			else if (arg == L"--yara-timeout") {
				size_t seconds;
				if (!value(v) || !ParseNumber(v, seconds) || seconds > 3600) {
					Error("--yara-timeout needs a number of seconds (at most 3600)");
					return false;
				}
				options.Report.YaraTimeout = (int)seconds;
			}
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
			else if (arg == L"--by") {
				if (!value(v))
					return false;
				if (v == L"tlsh")
					options.ClusterBy = ClusterBy::Tlsh;
				else if (v == L"ssdeep")
					options.ClusterBy = ClusterBy::Ssdeep;
				else if (v == L"imphash")
					options.ClusterBy = ClusterBy::Imphash;
				else {
					Error("--by needs tlsh, ssdeep or imphash");
					return false;
				}
			}
			else if (arg == L"--threshold") {
				size_t n;
				if (!value(v) || !ParseNumber(v, n) || n > 1000) {
					Error("--threshold needs a number (at most 1000)");
					return false;
				}
				options.Threshold = (int)n;
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
		if (options.Report.Yara)	// libyara scans from at most this many threads at once
			threads = std::min(threads, YaraRules::MaxThreads);
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

	//
	// cluster
	//

	struct ClusterItem {
		std::wstring Path;
		JsonValue File;		// the file part of the report: the hashes
		std::string Ssdeep, Tlsh, Imphash;
		uint64_t BlockSize{};	// of the ssdeep digest
	};

	// How alike two files are by the measure of the options, and whether that is close enough to put them together.
	// TLSH is a distance (less is closer), ssdeep a score (more is closer).
	struct Similarity {
		ClusterBy By;
		int Threshold;

		bool Comparable(ClusterItem const& a, ClusterItem const& b) const {
			switch (By) {
				case ClusterBy::Tlsh: return !a.Tlsh.empty() && !b.Tlsh.empty();
				case ClusterBy::Ssdeep: return a.BlockSize == b.BlockSize || a.BlockSize == 2 * b.BlockSize || b.BlockSize == 2 * a.BlockSize;
				default: return !a.Imphash.empty() && a.Imphash == b.Imphash;
			}
		}
		int Score(ClusterItem const& a, ClusterItem const& b) const {
			switch (By) {
				case ClusterBy::Tlsh: return FuzzyHash::TlshDistance(a.Tlsh, b.Tlsh);
				case ClusterBy::Ssdeep: return FuzzyHash::CompareSsdeep(a.Ssdeep, b.Ssdeep);
				default: return 0;
			}
		}
		bool Close(int score) const {
			switch (By) {
				case ClusterBy::Tlsh: return score >= 0 && score <= Threshold;
				case ClusterBy::Ssdeep: return score > 0 && score >= Threshold;
				default: return true;
			}
		}
		// whether score a is closer than b
		bool Closer(int a, int b) const {
			return By == ClusterBy::Tlsh ? a < b : a > b;
		}
	};

	struct ClusterGroup {
		std::vector<size_t> Members;	// the center first (by TLSH or ssdeep)
		std::vector<int> Scores;		// of each to the center
	};

	// The pairs of items that are close, found on worker threads (each takes the rows of the triangle in turn)
	std::vector<std::pair<size_t, size_t>> FindClosePairs(std::vector<ClusterItem> const& items, Similarity const& similarity, Options const& options) {
		std::vector<std::pair<size_t, size_t>> pairs;
		if (similarity.By == ClusterBy::Imphash) {
			// equal hashes: sorting is enough
			std::vector<size_t> order(items.size());
			std::iota(order.begin(), order.end(), 0);
			std::ranges::sort(order, {}, [&](size_t i) { return std::tie(items[i].Imphash, i); });
			for (size_t k = 1; k < order.size(); k++)
				if (similarity.Comparable(items[order[k - 1]], items[order[k]]))
					pairs.emplace_back(order[k - 1], order[k]);
			return pairs;
		}
		std::mutex lock;
		std::atomic<size_t> next{ 0 };
		auto threads = options.Threads ? options.Threads : std::max(1u, std::thread::hardware_concurrency());
		{
			std::vector<std::jthread> workers;
			for (unsigned t = 0; t < threads; t++)
				workers.emplace_back([&] {
					std::vector<std::pair<size_t, size_t>> found;
					for (size_t i; !g_Cancel && (i = next++) < items.size(); )
						for (size_t j = i + 1; j < items.size(); j++)
							if (similarity.Comparable(items[i], items[j]) && similarity.Close(similarity.Score(items[i], items[j])))
								found.emplace_back(i, j);
					std::lock_guard guard(lock);
					pairs.insert(pairs.end(), found.begin(), found.end());
				});
		}
		return pairs;
	}

	// Star clustering: the file that is close to the most others (not yet in a cluster) is a center, and makes a cluster
	// with them; then the next. Every file of a cluster is then close to its center, which chaining (single linkage)
	// would not give: a chain of files each a little different from the last ends far from where it started.
	// By imphash, the files with the same imphash.
	std::pair<std::vector<ClusterGroup>, std::vector<ClusterGroup>> MakeClusters(std::vector<ClusterItem> const& items,
		Similarity const& similarity, Options const& options) {
		std::vector<ClusterGroup> clusters, singles;
		auto byPath = [&](size_t a, size_t b) { return _wcsicmp(items[a].Path.c_str(), items[b].Path.c_str()) < 0; };
		if (similarity.By == ClusterBy::Imphash) {
			std::map<std::string, std::vector<size_t>> groups;
			for (size_t i = 0; i < items.size(); i++)
				if (!items[i].Imphash.empty())
					groups[items[i].Imphash].push_back(i);
				else
					singles.push_back({ { i } });
			for (auto& [hash, members] : groups) {
				std::ranges::sort(members, byPath);
				(members.size() > 1 ? clusters : singles).push_back({ std::move(members) });
			}
		}
		else {
			std::vector<std::vector<size_t>> close(items.size());
			for (auto [a, b] : FindClosePairs(items, similarity, options))
				close[a].push_back(b), close[b].push_back(a);
			std::vector<bool> taken(items.size());
			std::vector<size_t> open(items.size());	// the close files not yet in a cluster
			for (size_t i = 0; i < items.size(); i++)
				open[i] = close[i].size();
			for (;;) {
				size_t center = 0, most = 0;
				for (size_t i = 0; i < items.size(); i++)
					if (!taken[i] && open[i] > most)
						most = open[i], center = i;
				if (most == 0)
					break;
				ClusterGroup g{ { center } };
				for (auto m : close[center])
					if (!taken[m])
						g.Members.push_back(m);
				for (auto m : g.Members) {
					taken[m] = true;
					for (auto n : close[m])
						open[n]--;
				}
				for (auto m : g.Members)
					g.Scores.push_back(m == center ? 0 : similarity.Score(items[center], items[m]));
				// the closest first
				std::vector<size_t> order(g.Members.size() - 1);
				std::iota(order.begin(), order.end(), 1);
				std::ranges::sort(order, [&](size_t a, size_t b) {
					return g.Scores[a] != g.Scores[b] ? similarity.Closer(g.Scores[a], g.Scores[b]) : byPath(g.Members[a], g.Members[b]);
				});
				ClusterGroup sorted{ { center }, { g.Scores[0] } };
				for (auto k : order)
					sorted.Members.push_back(g.Members[k]), sorted.Scores.push_back(g.Scores[k]);
				clusters.push_back(std::move(sorted));
			}
			for (size_t i = 0; i < items.size(); i++)
				if (!taken[i])
					singles.push_back({ { i } });
		}
		std::ranges::stable_sort(clusters, std::greater{}, [](ClusterGroup const& g) { return g.Members.size(); });
		std::ranges::sort(singles, byPath, [](ClusterGroup const& g) { return g.Members[0]; });
		return { std::move(clusters), std::move(singles) };
	}

	int Cluster(Options const& options) {
		if (options.Inputs.empty()) {
			Error("cluster needs at least one directory, file or wildcard");
			return UsageError;
		}
		Similarity similarity{ options.ClusterBy, options.Threshold >= 0 ? options.Threshold
			: options.ClusterBy == ClusterBy::Tlsh ? DefaultTlshThreshold : DefaultSsdeepThreshold };
		Output file, csvFile;
		auto out = OpenOutput(options.OutputPath, file);
		if (!out)
			return UsageError;
		Output* csv = nullptr;
		if (!options.CsvPath.empty() && !(csv = OpenOutput(options.CsvPath, csvFile)))
			return UsageError;

		// the hashes of the files (only the file part of the report)
		auto report = options.Report;
		report.Parts = ReportPart::File;
		report.Yara = nullptr;
		bool problems;
		auto files = Expand(options, problems);
		std::vector<ClusterItem> items;
		size_t failed = 0, skipped = 0;
		bool complete = RunOrdered<FileResult>(files.size(), options,
			[&](size_t i) { return AnalyzeFile(files[i], report); },
			[&](size_t i, FileResult& result) {
				if (result.Status == FileStatus::NotPE) {
					skipped++;
					return;
				}
				if (result.Status != FileStatus::Ok) {
					failed++;
					Error(std::format("{}: {}", ToUtf8(files[i]), result.Report["error"].AsString()));
					return;
				}
				ClusterItem item{ files[i], result.Report["file"] };
				item.Ssdeep = item.File["ssdeep"].AsString();
				item.Tlsh = item.File["tlsh"].AsString();
				item.Imphash = item.File["imphash"].AsString();
				std::from_chars(item.Ssdeep.data(), item.Ssdeep.data() + item.Ssdeep.size(), item.BlockSize);
				items.push_back(std::move(item));
			},
			[&](size_t i) { return FileName(files[i]); });
		if (!complete) {
			Error("cancelled");
			return SomeFailed;
		}

		auto [clusters, singles] = MakeClusters(items, similarity, options);

		auto byName = similarity.By == ClusterBy::Tlsh ? "tlsh" : similarity.By == ClusterBy::Ssdeep ? "ssdeep" : "imphash";
		auto member = [&](size_t m, int score) {
			auto object = items[m].File.AsObject();
			if (score >= 0 && similarity.By != ClusterBy::Imphash)
				object["score"] = (double)score;
			return JsonValue(std::move(object));
		};
		if (csv) {
			csv->Write("\xEF\xBB\xBF");
			csv->WriteLine(CsvRow({ "cluster", "center", "score", "path", "size", "sha256", "imphash", "ssdeep", "tlsh" }));
			auto row = [&](std::string const& cluster, size_t m, bool center, int score) {
				auto const& f = items[m].File;
				csv->WriteLine(CsvRow({ cluster, center ? "true" : "false", score < 0 || center ? "" : std::to_string(score), f["path"].AsString(),
					f["size"].Write(false), f["sha256"].AsString(), f["imphash"].AsString(), f["ssdeep"].AsString(), f["tlsh"].AsString() }));
			};
			for (size_t c = 0; c < clusters.size(); c++)
				for (size_t k = 0; k < clusters[c].Members.size(); k++)
					row(std::to_string(c + 1), clusters[c].Members[k], k == 0 && similarity.By != ClusterBy::Imphash,
						clusters[c].Scores.empty() ? -1 : clusters[c].Scores[k]);
			if (options.Verbose)
				for (auto const& s : singles)
					row("", s.Members[0], false, -1);
		}
		if (options.Text) {
			for (size_t c = 0; c < clusters.size(); c++) {
				auto const& g = clusters[c];
				out->WriteLine(std::format("cluster {}: {} files{}", c + 1, g.Members.size(),
					similarity.By == ClusterBy::Imphash ? " (imphash " + items[g.Members[0]].Imphash + ")" : ""));
				for (size_t k = 0; k < g.Members.size(); k++) {
					auto mark = similarity.By == ClusterBy::Imphash ? "  " : k == 0 ? "* " : std::format("{:>5} ", g.Scores[k] < 0 ? "-" : std::to_string(g.Scores[k]));
					out->WriteLine(std::format("  {}{}", mark, ToUtf8(items[g.Members[k]].Path)));
				}
				out->WriteLine("");
			}
			if (options.Verbose && !singles.empty()) {
				out->WriteLine(std::format("in no cluster: {} files", singles.size()));
				for (auto const& s : singles)
					out->WriteLine("    " + ToUtf8(items[s.Members[0]].Path));
			}
		}
		else if (!csv || !options.OutputPath.empty()) {
			JsonValue::Array list;
			for (auto const& g : clusters) {
				JsonValue::Array members;
				for (size_t k = 0; k < g.Members.size(); k++)
					members.push_back(member(g.Members[k], k == 0 || g.Scores.empty() ? -1 : g.Scores[k]));
				JsonValue::Object cluster{ { "size", (double)g.Members.size() }, { "files", std::move(members) } };
				if (similarity.By == ClusterBy::Imphash)
					cluster["imphash"] = items[g.Members[0]].Imphash;
				list.push_back(std::move(cluster));
			}
			JsonValue::Array unclustered;
			for (auto const& s : singles)
				unclustered.push_back(options.Verbose ? member(s.Members[0], -1) : JsonValue(ToUtf8(items[s.Members[0]].Path)));
			JsonValue::Object result{
				{ "schema", ClusterSchema },
				{ "by", byName },
				{ "threshold", similarity.By == ClusterBy::Imphash ? JsonValue() : JsonValue((double)similarity.Threshold) },
				{ "files", (double)items.size() },
				{ "failed", (double)failed },
				{ "skipped", (double)skipped },
				{ "clusters", std::move(list) },
				{ "unclustered", std::move(unclustered) },
			};
			out->WriteLine(WithTool(result).Write(!options.Compact));
		}
		if (!options.Quiet) {
			auto measure = similarity.By == ClusterBy::Tlsh ? std::format("TLSH distance at most {}", similarity.Threshold)
				: similarity.By == ClusterBy::Ssdeep ? std::format("ssdeep score at least {}", similarity.Threshold) : "the same imphash";
			Output::StdErr().WriteLine(std::format("{} files: {} clusters of {} files ({}), {} in no cluster, {} failed, {} skipped (not PE)",
				files.size(), clusters.size(), items.size() - singles.size(), measure, singles.size(), failed, skipped));
		}
		return failed || problems ? SomeFailed : Success;
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
		Output::StdOut().WriteLine(std::format("{} {} (schemas {}, {}, {})", ToolName, ToolVersion, ReportSchema, DiffSchema, ClusterSchema));
		return Success;
	}
	if (argc < 2 || wcscmp(argv[1], L"-h") == 0 || wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"help") == 0) {
		Output::StdOut().Write(Usage);
		return argc < 2 ? UsageError : Success;
	}
	Options options;
	if (!ParseArguments(argc, argv, options))
		return UsageError;

	// the rules are compiled once, for all the files (and threads)
	YaraRules yara;
	if (!options.YaraPath.empty()) {
		bool loaded = yara.Load(options.YaraPath);
		size_t errors = 0, warnings = 0;
		for (auto const& d : yara.Diagnostics()) {
			(d.Error ? errors : warnings)++;
			if (d.Error || options.Verbose)
				Error(std::format("{}: {}({}): {}", d.Error ? "yara error" : "yara warning", ToUtf8(d.File), d.Line, d.Message));
		}
		if (!loaded) {
			Error(std::format("no YARA rules could be loaded from {}", ToUtf8(options.YaraPath)));
			return UsageError;
		}
		if (!options.Quiet)
			Error(std::format("{} YARA rule{} loaded{}{}", yara.RuleCount(), yara.RuleCount() == 1 ? "" : "s",
				errors ? std::format(", {} error{} (rule files left out)", errors, errors == 1 ? "" : "s") : "",
				warnings && !options.Verbose ? std::format(", {} warning{} (-v lists them)", warnings, warnings == 1 ? "" : "s") : ""));
		options.Report.Yara = &yara;
	}

	CapabilityRules capabilities;
	if (!options.CapabilitiesPath.empty()) {
		capabilities.Add(CapabilityRules::BuiltIn());
		std::vector<std::string> errors;
		auto before = capabilities.Size();
		capabilities.AddPath(options.CapabilitiesPath, errors);
		for (auto const& e : errors)
			Error("capability rules: " + e);
		if (capabilities.Size() == before) {
			Error(std::format("no capability rules could be loaded from {}", ToUtf8(options.CapabilitiesPath)));
			return UsageError;
		}
		if (!options.Quiet)
			Error(std::format("{} capability rules ({} of them from {})", capabilities.Size(), capabilities.Size() - before, ToUtf8(options.CapabilitiesPath)));
		options.Report.Capabilities = &capabilities;
	}

	if (options.Command == L"info")
		return Info(options);
	if (options.Command == L"scan")
		return Scan(options);
	if (options.Command == L"diff")
		return Diff(options);
	if (options.Command == L"check")
		return Check(options);
	if (options.Command == L"cluster")
		return Cluster(options);
	Error(std::format("unknown command: {} (try tpe --help)", ToUtf8(options.Command)));
	return UsageError;
}
