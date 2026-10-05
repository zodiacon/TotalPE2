#include "pch.h"
#include "EventManifestView.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewHelper.h>
#include <ToolbarHelper.h>

CEventManifestView::CEventManifestView(IMainFrame* frame, PCWSTR title, EventManifest manifest, std::wstring const& path)
	: CViewBase(frame), m_Title(title), m_Manifest(std::move(manifest)) {
	// as a resource DLL: the messages may be in the .mui file of the file, which the loader finds
	m_Messages = ::LoadLibraryEx(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	BuildTables();
}

CEventManifestView::~CEventManifestView() {
	if (m_Messages)
		::FreeLibrary(m_Messages);
}

CString CEventManifestView::GetTitle() const {
	return m_Title;
}

// the text of a message of the file, on one line; empty if there is none
std::wstring CEventManifestView::Message(uint32_t id) const {
	if (id == NoEventMessage || !m_Messages)
		return {};
	PWSTR buffer = nullptr;
	auto len = ::FormatMessage(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
		m_Messages, id, 0, (PWSTR)&buffer, 0, nullptr);
	if (len == 0 || buffer == nullptr)
		return {};
	std::wstring text(buffer, len);
	::LocalFree(buffer);
	for (auto& c : text)
		if (c == L'\r' || c == L'\n' || c == L'\t')
			c = L' ';
	while (!text.empty() && text.back() == L' ')
		text.pop_back();
	return text;
}

namespace {
	std::wstring Dec(uint64_t value) { return std::to_wstring(value); }
	std::wstring Hex(uint64_t value) { return std::format(L"0x{:X}", value); }
	std::wstring Wide(std::string const& text) { return std::wstring(text.begin(), text.end()); }

	// "name (value)", or the value if there is no name
	std::wstring Named(std::wstring const& name, uint64_t value) {
		return name.empty() ? Dec(value) : std::format(L"{} ({})", name, value);
	}

	// the numbers of cells sort as numbers
	std::optional<uint64_t> AsNumber(std::wstring const& text) {
		if (text.empty())
			return std::nullopt;
		wchar_t* end = nullptr;
		auto value = text.starts_with(L"0x") ? wcstoull(text.c_str() + 2, &end, 16) : wcstoull(text.c_str(), &end, 10);
		return end && *end == 0 ? std::optional(value) : std::nullopt;
	}
}

void CEventManifestView::BuildTables() {
	auto& events = m_Tables[(int)Table::Events];
	auto& templates = m_Tables[(int)Table::Templates];
	auto& providers = m_Tables[(int)Table::Providers];
	auto& channels = m_Tables[(int)Table::Channels];
	auto& levels = m_Tables[(int)Table::Levels];
	auto& tasks = m_Tables[(int)Table::Tasks];
	auto& opcodes = m_Tables[(int)Table::Opcodes];
	auto& keywords = m_Tables[(int)Table::Keywords];
	auto& maps = m_Tables[(int)Table::Maps];

	bool many = m_Manifest.Providers.size() > 1;
	auto withProvider = [&](std::vector<Column> columns) {
		if (many)
			columns.insert(columns.begin(), { L"Provider", 220 });
		return columns;
	};
	events = { L"Events", withProvider({ { L"ID", 60, LVCFMT_RIGHT }, { L"Version", 55, LVCFMT_RIGHT }, { L"Channel", 160 },
		{ L"Level", 150 }, { L"Task", 140 }, { L"Opcode", 110 }, { L"Keywords", 200 }, { L"Fields", 300 }, { L"Message", 500 } }) };
	templates = { L"Templates", withProvider({ { L"Template", 75, LVCFMT_RIGHT }, { L"Used By", 120 }, { L"#", 35, LVCFMT_RIGHT },
		{ L"Field", 200 }, { L"In Type", 160 }, { L"Out Type", 140 }, { L"Map", 160 }, { L"Count", 50, LVCFMT_RIGHT },
		{ L"Length", 50, LVCFMT_RIGHT }, { L"Flags", 60 } }) };
	providers = { L"Providers", { { L"Name", 280 }, { L"GUID", 280 }, { L"Message", 250 }, { L"Events", 60, LVCFMT_RIGHT },
		{ L"Templates", 70, LVCFMT_RIGHT }, { L"Channels", 65, LVCFMT_RIGHT } } };
	channels = { L"Channels", withProvider({ { L"Value", 55, LVCFMT_RIGHT }, { L"Name", 360 }, { L"Kind", 90 }, { L"Message", 300 } }) };
	levels = { L"Levels", withProvider({ { L"Value", 55, LVCFMT_RIGHT }, { L"Name", 200 }, { L"Message", 300 } }) };
	tasks = { L"Tasks", withProvider({ { L"Value", 55, LVCFMT_RIGHT }, { L"Name", 220 }, { L"Event GUID", 280 }, { L"Message", 300 } }) };
	opcodes = { L"Opcodes", withProvider({ { L"Value", 55, LVCFMT_RIGHT }, { L"Task", 160 }, { L"Name", 200 }, { L"Message", 300 } }) };
	keywords = { L"Keywords", withProvider({ { L"Mask", 150 }, { L"Name", 220 }, { L"Message", 300 } }) };
	maps = { L"Maps", withProvider({ { L"Map", 220 }, { L"Kind", 70 }, { L"Value", 90 }, { L"Message", 400 } }) };

	for (auto const& p : m_Manifest.Providers) {
		auto pname = p.Name.empty() ? Wide(p.Guid) : p.Name;
		auto add = [&](TableData& table, std::vector<std::wstring> cells) {
			if (many)
				cells.insert(cells.begin(), pname);
			table.Rows.push_back(std::move(cells));
		};
		auto taskName = [&](uint32_t value) {
			for (auto const& t : p.Tasks)
				if (t.Value == value)
					return t.Name;
			return std::wstring();
		};

		auto pmsg = Message(p.MessageId);
		providers.Rows.push_back({ pname, Wide(p.Guid), pmsg, Dec(p.Events.size()), Dec(p.Templates.size()), Dec(p.Channels.size()) });

		for (auto const& c : p.Channels)
			add(channels, { Dec(c.Value), c.Name, c.Flags & 1 ? L"Imported" : L"Provider", Message(c.MessageId) });
		for (auto const& l : p.Levels)
			add(levels, { Dec(l.Value), l.Name, Message(l.MessageId) });
		for (auto const& t : p.Tasks)
			add(tasks, { Dec(t.Value), t.Name, Wide(t.Guid), Message(t.MessageId) });
		for (auto const& o : p.Opcodes)
			add(opcodes, { Dec(o.Value), o.Task ? Named(taskName(o.Task), o.Task) : L"", o.Name, Message(o.MessageId) });
		for (auto const& k : p.Keywords)
			add(keywords, { std::format(L"0x{:016X}", k.Mask), k.Name, Message(k.MessageId) });
		for (auto const& m : p.Maps)
			for (auto const& e : m.Entries)
				add(maps, { m.Name, m.Bitmap ? L"Bitmap" : L"Values", m.Bitmap ? Hex(e.Value) : Dec(e.Value), Message(e.MessageId) });

		// the events that use each template
		std::vector<std::wstring> users(p.Templates.size());
		for (auto const& ev : p.Events)
			if (ev.Template >= 0) {
				auto& u = users[ev.Template];
				if (u.size() < 100)
					u += (u.empty() ? L"" : L", ") + Dec(ev.Id);
			}
		for (size_t t = 0; t < p.Templates.size(); t++) {
			auto const& temp = p.Templates[t];
			int index = 0;
			for (auto const& f : temp.Fields) {
				std::wstring in = EventInTypeName(f.InType), out = EventOutTypeName(f.OutType);
				add(templates, { Dec(t), users[t], Dec(index++), f.Name, in.empty() ? Dec(f.InType) : in, out.empty() ? Dec(f.OutType) : out,
					f.Map >= 0 ? p.Maps[f.Map].Name : L"", f.Count ? Dec(f.Count) : L"", f.Length ? Dec(f.Length) : L"",
					f.Flags ? Hex(f.Flags) : L"" });
			}
			if (temp.Fields.empty())
				add(templates, { Dec(t), users[t], L"", L"(no fields)", L"", L"", L"", L"", L"", L"" });
		}

		for (auto const& ev : p.Events) {
			std::wstring channel, level, task, opcode, kw, fields;
			if (ev.ChannelIndex >= 0)
				channel = Named(p.Channels[ev.ChannelIndex].Name, ev.Channel);
			else if (ev.Channel)
				channel = Dec(ev.Channel);
			level = ev.LevelIndex >= 0 ? Named(p.Levels[ev.LevelIndex].Name, ev.Level) : Dec(ev.Level);
			if (ev.TaskIndex >= 0)
				task = Named(p.Tasks[ev.TaskIndex].Name, ev.Task);
			else if (ev.Task)
				task = Dec(ev.Task);
			opcode = ev.OpcodeIndex >= 0 ? Named(p.Opcodes[ev.OpcodeIndex].Name, ev.Opcode) : Dec(ev.Opcode);
			for (auto k : ev.KeywordIndices)
				kw += (kw.empty() ? L"" : L", ") + p.Keywords[k].Name;
			if (kw.empty() && ev.Keywords)
				kw = std::format(L"0x{:X}", ev.Keywords);
			if (ev.Template >= 0)
				for (auto const& f : p.Templates[ev.Template].Fields)
					fields += (fields.empty() ? L"" : L", ") + f.Name;
			add(events, { Dec(ev.Id), Dec(ev.Version), channel, level, task, opcode, kw, fields, Message(ev.MessageId) });
		}
	}
}

CString CEventManifestView::GetColumnText(HWND, int row, int col) const {
	auto const& table = m_Tables[(int)m_Table];
	if (row < 0 || row >= (int)m_Rows.size())
		return CString();
	auto const& cells = table.Rows[m_Rows[row]];
	auto c = GetColumnManager(m_List)->GetColumnTag<int>(col);
	return c >= 0 && c < (int)cells.size() ? cells[c].c_str() : L"";
}

void CEventManifestView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;
	auto c = GetColumnManager(m_List)->GetColumnTag<int>(si->SortColumn);
	auto asc = si->SortAscending;
	auto const& rows = m_Tables[(int)m_Table].Rows;
	std::ranges::stable_sort(m_Rows, [&](int a, int b) {
		auto const& x = rows[a][c], & y = rows[b][c];
		auto nx = AsNumber(x), ny = AsNumber(y);
		if (nx && ny)
			return SortHelper::Sort(*nx, *ny, asc);
		return SortHelper::Sort(x, y, asc);
	});
}

void CEventManifestView::OnStateChanged(HWND, int, int, UINT oldState, UINT newState) const {
	if ((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED))
		UpdateUI();
}

void CEventManifestView::UpdateUI(bool) const {
	Frame()->GetUI().UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
	size_t events = 0;
	for (auto const& p : m_Manifest.Providers)
		events += p.Events.size();
	Frame()->SetStatusText(1, std::format(L"Manifest version {}.{}: {} provider{}, {} events", m_Manifest.MajorVersion, m_Manifest.MinorVersion,
		m_Manifest.Providers.size(), m_Manifest.Providers.size() == 1 ? L"" : L"s", events).c_str());
}

void CEventManifestView::ShowTable(Table table) {
	m_Table = table;
	for (int id = ID_EVENTS_FIRST; id <= ID_EVENTS_LAST; id++)
		UISetCheck(id, id - ID_EVENTS_FIRST == (int)table);

	// the columns of the table
	m_List.SetItemCount(0);
	auto cm = GetColumnManager(m_List);
	cm->Clear();
	auto const& data = m_Tables[(int)table];
	for (int i = 0; i < (int)data.Columns.size(); i++)
		cm->AddColumn(data.Columns[i].Name, data.Columns[i].Format, data.Columns[i].Width, i);
	ClearSort(m_List);
	ApplyFilter();
}

void CEventManifestView::ApplyFilter() {
	CString filter(m_FilterText);
	filter.MakeLower();
	m_Rows.clear();
	auto const& rows = m_Tables[(int)m_Table].Rows;
	for (int i = 0; i < (int)rows.size(); i++) {
		if (!filter.IsEmpty()) {
			std::wstring all;
			for (auto const& cell : rows[i])
				all += cell + L"\t";
			CString text(all.c_str());
			text.MakeLower();
			if (text.Find(filter) < 0)
				continue;
		}
		m_Rows.push_back(i);
	}
	if (auto si = GetSortInfo(m_List); si && si->SortColumn >= 0)
		DoSort(si);
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL);
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
}

LRESULT CEventManifestView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	// a button for each table, with the number of rows
	std::vector<std::wstring> texts;
	for (auto const& t : m_Tables)
		texts.push_back(std::format(L"{} ({})", t.Name, t.Rows.size()));
	std::vector<ToolBarButtonInfo> buttons;
	for (int i = 0; i < (int)m_Tables.size(); i++)
		buttons.push_back({ (UINT)(ID_EVENTS_FIRST + i), 0, BTNS_CHECKGROUP | BTNS_SHOWTEXT | BTNS_AUTOSIZE, texts[i].c_str() });
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_tb = ToolbarHelper::CreateAndInitToolBar(m_hWnd, buttons.data(), (int)buttons.size(), 16);
	AddSimpleReBarBand(m_tb);
	UIAddToolBar(m_tb);

	CRect rc(0, 0, 250, 20);
	m_Filter.Create(m_hWnd, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL);
	m_Filter.SetFont(AtlGetDefaultGuiFont());
	m_Filter.SetWatermark(L"Filter (Esc to clear)");
	AddSimpleReBarBand(m_Filter, nullptr, FALSE, 250);

	// the band of the toolbar is as wide as its buttons, so the filter comes after it
	CRect rcLast;
	m_tb.GetItemRect(m_tb.GetButtonCount() - 1, &rcLast);
	CReBarCtrl rebar(m_hWndToolBar);
	REBARBANDINFO band{ sizeof(band) };
	band.fMask = RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_IDEALSIZE;
	rebar.GetBandInfo(0, &band);
	band.cxMinChild = band.cxIdeal = rcLast.right;
	band.cx = rcLast.right + 16;
	rebar.SetBandInfo(0, &band);

	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	ShowTable(Table::Events);
	return 0;
}

LRESULT CEventManifestView::OnTable(WORD, WORD id, HWND, BOOL&) {
	ShowTable((Table)(id - ID_EVENTS_FIRST));
	return 0;
}

LRESULT CEventManifestView::OnFilterChanged(WORD, WORD, HWND hWnd, BOOL& handled) {
	if (hWnd != m_Filter) {
		handled = FALSE;
		return 0;
	}
	m_Filter.GetWindowText(m_FilterText);
	ApplyFilter();
	return 0;
}

LRESULT CEventManifestView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
	auto findDlg = Frame()->GetFindDialog();
	if (findDlg == nullptr || m_List.GetItemCount() == 0)
		return 0;
	auto index = ListViewHelper::SearchItem(m_List, findDlg->GetFindString(), findDlg->SearchDown(), findDlg->MatchCase());
	if (index >= 0) {
		m_List.SelectItem(index);
		m_List.SetFocus();
	}
	else
		AtlMessageBox(m_hWnd, L"Finished searching list.", IDR_MAINFRAME, MB_ICONINFORMATION);
	return 0;
}

LRESULT CEventManifestView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List));
	return 0;
}
