#include "pch.h"
#include "DocPane.h"
#include "AppSettings.h"
#include <wrl/event.h>

using Microsoft::WRL::Callback;

namespace {
	constexpr UINT_PTR TimerLoad = 1;

	bool Dark() {
		return AppSettings::Get().DarkMode();
	}
	COLORREF Background() {
		return Dark() ? RGB(32, 32, 32) : ::GetSysColor(COLOR_WINDOW);
	}
	COLORREF Foreground() {
		return Dark() ? RGB(220, 220, 220) : ::GetSysColor(COLOR_WINDOWTEXT);
	}

	std::wstring Escaped(std::wstring const& text) {
		std::wstring result;
		for (auto c : text) {
			switch (c) {
				case L'<': result += L"&lt;"; break;
				case L'>': result += L"&gt;"; break;
				case L'&': result += L"&amp;"; break;
				default: result += c;
			}
		}
		return result;
	}
}

void CDocPane::Navigate(std::wstring url, std::wstring const& name) {
	m_Url = std::move(url);
	m_Name = name;
	if (!m_Controller) {
		Start();
		return;
	}
	SetTimer(TimerLoad, 300);
}

// The browser, with its data (its cache, cookies) in the user's folder of the application (not next to the executable: it
// may be in a folder that cannot be written to)
void CDocPane::Start() {
	if (m_Starting || m_Controller)
		return;
	m_Starting = true;
	m_Message = L"Starting the browser...";
	Invalidate();
	WCHAR folder[MAX_PATH];
	::ExpandEnvironmentStrings(L"%LOCALAPPDATA%\\TotalPE\\WebView2", folder, _countof(folder));
	auto hWnd = m_hWnd;
	auto hr = ::CreateCoreWebView2EnvironmentWithOptions(nullptr, folder, nullptr,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this, hWnd](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
			// (the window may have been closed meanwhile: this is no more)
			if (!::IsWindow(hWnd))
				return S_OK;
			if (FAILED(result) || !environment) {
				m_Starting = false;
				m_Message = std::format(L"The browser could not be started (error 0x{:08X}).", (uint32_t)result);
				Invalidate();
				return S_OK;
			}
			return environment->CreateCoreWebView2Controller(hWnd,
				Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([this, hWnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
					if (!::IsWindow(hWnd)) {
						if (controller)
							controller->Close();
						return S_OK;
					}
					m_Starting = false;
					if (FAILED(result) || !controller) {
						m_Message = std::format(L"The browser could not be started (error 0x{:08X}).", (uint32_t)result);
						Invalidate();
						return S_OK;
					}
					Started(controller);
					return S_OK;
				}).Get());
		}).Get());
	if (FAILED(hr)) {
		m_Starting = false;
		m_Message = hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ?
			L"The documentation is shown by Microsoft Edge WebView2, which is not installed." :
			std::format(L"The browser could not be started (error 0x{:08X}).", (uint32_t)hr);
		Invalidate();
	}
}

void CDocPane::Started(ICoreWebView2Controller* controller) {
	m_Controller = controller;
	m_Controller->get_CoreWebView2(&m_WebView);
	if (!m_WebView)
		return;
	Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
	if (SUCCEEDED(m_WebView->get_Settings(&settings))) {
		settings->put_AreDevToolsEnabled(FALSE);
		settings->put_IsStatusBarEnabled(FALSE);
	}
	// a link that opens a window: in the default browser
	m_WebView->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
		[](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
			wil::unique_cotaskmem_string uri;
			if (SUCCEEDED(args->get_Uri(&uri)) && uri)
				::ShellExecute(nullptr, L"open", uri.get(), nullptr, nullptr, SW_SHOWNORMAL);
			args->put_Handled(TRUE);
			return S_OK;
		}).Get(), nullptr);
	// a page of the documentation: from where its text starts (not its site's banners and links)
	m_WebView->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
		[](ICoreWebView2* webView, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
			BOOL ok = FALSE;
			wil::unique_cotaskmem_string uri;
			if (SUCCEEDED(args->get_IsSuccess(&ok)) && ok && SUCCEEDED(webView->get_Source(&uri)) && uri &&
				std::wstring_view(uri.get()).starts_with(L"https://learn.microsoft.com/"))
				webView->ExecuteScript(L"document.querySelector('main')?.scrollIntoView()", nullptr);
			return S_OK;
		}).Get(), nullptr);
	ApplyColors();
	CRect rc;
	GetClientRect(&rc);
	m_Controller->put_Bounds(rc);
	m_Controller->put_IsVisible(IsWindowVisible());
	m_Message.clear();
	Load();
}

// the page asked for, if it is not the one loaded (none: a page that says so)
void CDocPane::Load() {
	if (!m_WebView || m_Url == m_Loaded)
		return;
	m_Loaded = m_Url;
	if (!m_Url.empty()) {
		m_WebView->Navigate(m_Url.c_str());
		return;
	}
	auto color = [](COLORREF c) { return std::format(L"#{:02x}{:02x}{:02x}", GetRValue(c), GetGValue(c), GetBValue(c)); };
	auto text = m_Name.empty() ? std::wstring() : L"Microsoft's documentation has no page for " + Escaped(m_Name) + L".";
	auto html = std::format(L"<html><body style=\"font-family: Segoe UI; font-size: 10pt; background: {}; color: {}\">{}</body></html>",
		color(Background()), color(Foreground()), text);
	m_WebView->NavigateToString(html.c_str());
}

// the pages' colors (those that have both) as the application's
void CDocPane::ApplyColors() {
	Microsoft::WRL::ComPtr<ICoreWebView2_13> webView;
	Microsoft::WRL::ComPtr<ICoreWebView2Profile> profile;
	if (m_WebView && SUCCEEDED(m_WebView.As(&webView)) && SUCCEEDED(webView->get_Profile(&profile)))
		profile->put_PreferredColorScheme(Dark() ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT);
	Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller;
	if (m_Controller && SUCCEEDED(m_Controller.As(&controller))) {
		auto c = Background();
		controller->put_DefaultBackgroundColor({ 255, GetRValue(c), GetGValue(c), GetBValue(c) });
	}
}

LRESULT CDocPane::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Controller) {
		CRect rc;
		GetClientRect(&rc);
		m_Controller->put_Bounds(rc);
	}
	Invalidate();
	return 0;
}

LRESULT CDocPane::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
	CPaintDC dc(m_hWnd);
	CRect rc;
	GetClientRect(&rc);
	dc.FillSolidRect(&rc, Background());
	if (!m_Controller && !m_Message.empty()) {
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(Foreground());
		auto font = dc.SelectFont(AtlGetDefaultGuiFont());
		rc.DeflateRect(8, 8);
		dc.DrawText(m_Message.c_str(), -1, &rc, DT_WORDBREAK | DT_NOPREFIX);
		dc.SelectFont(font);
	}
	return 0;
}

LRESULT CDocPane::OnTimer(UINT, WPARAM id, LPARAM, BOOL& handled) {
	if (id != TimerLoad) {
		handled = FALSE;
		return 0;
	}
	KillTimer(TimerLoad);
	Load();
	return 0;
}

LRESULT CDocPane::OnShowWindow(UINT, WPARAM shown, LPARAM, BOOL& handled) {
	if (m_Controller)
		m_Controller->put_IsVisible(shown ? TRUE : FALSE);
	handled = FALSE;
	return 0;
}

LRESULT CDocPane::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Controller)
		m_Controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
	return 0;
}

LRESULT CDocPane::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	if (m_Controller)
		m_Controller->Close();
	m_WebView.Reset();
	m_Controller.Reset();
	handled = FALSE;
	return 0;
}

LRESULT CDocPane::OnUpdateDarkMode(UINT, WPARAM, LPARAM, BOOL&) {
	ApplyColors();
	// (the page that says there is none has the colors in it)
	if (m_WebView && m_Url.empty()) {
		m_Loaded = L"\n";
		Load();
	}
	Invalidate();
	return 0;
}
