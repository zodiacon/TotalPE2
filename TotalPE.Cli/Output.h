#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <Json.h>

// Where UTF-8 text goes: a console (written as UTF-16, so the code page of the console does not matter), or a file or a pipe
// (the bytes as they are, with no CRLF translation)
class Output {
public:
	Output() = default;
	~Output();
	Output(Output const&) = delete;
	Output& operator=(Output const&) = delete;

	static Output& StdOut();
	static Output& StdErr();

	// a file that is created (or replaced); false if it cannot be
	bool Create(std::wstring const& path);

	void Write(std::string_view text);
	void WriteLine(std::string_view text) { Write(text); Write("\n"); }
	void Flush();
	bool IsConsole() const { return m_Console; }

private:
	void Attach(DWORD stdHandle);

	HANDLE m_Handle{ nullptr };
	bool m_Console{ false };
	bool m_Owned{ false };
	std::string m_Buffer;	// writes to files and pipes are gathered, there can be many small ones
};

std::string ToUtf8(std::wstring_view text);

// A row of a CSV file (RFC 4180): the values of the columns, quoted where they need to be
std::string CsvRow(std::vector<std::string> const& values);
// The values of a summary row (SummarizeReport) as the text of CSV fields: null is empty
std::vector<std::string> CsvValues(JsonValue const& row, std::vector<std::string> const& columns);

// A report as text for people
std::string FormatReportText(JsonValue const& report);

// A diff report (BuildDiffReport, BuildOneSidedDiffReport) as text for people
std::string FormatDiffText(JsonValue const& diff);
