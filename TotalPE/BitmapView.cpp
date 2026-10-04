#include "pch.h"
#include <wincodec.h>
#include "resource.h"
#include "BitmapView.h"
#include "ResourceContent.h"
#include "SaveData.h"

#pragma comment(lib, "msimg32")

CBitmapView::CBitmapView(IMainFrame* frame, PCWSTR title) : CViewBase(frame), m_Title(title) {
}

CString CBitmapView::GetTitle() const {
    return m_Title;
}

bool CBitmapView::CanSave() const {
    return !m_Data.empty();
}

LRESULT CBitmapView::OnSave(WORD, WORD, HWND, BOOL&) {
    if (m_Data.empty())
        return 0;
    auto file = MakeResourceFile(m_Data, m_Dib ? 2 : 10, L"");	// RT_BITMAP, or the type does not matter (RT_RCDATA)
    auto ext = CString(file.Extension.c_str());
    auto filter = std::format(L"{0} Images (*.{1})|*.{1}|All Files|*.*|", (PCWSTR)CString(ext).MakeUpper(), (PCWSTR)ext);
    std::ranges::replace(filter, L'|', L'\0');
    auto name = m_Title;
    if (auto paren = name.Find(L" ("); paren > 0)
        name = name.Left(paren);
    name.Remove(L'#');
    auto path = AskSaveFile(m_hWnd, L"Save Image", ext, ToFileName(name) + L"." + ext, filter.c_str());
    if (!path.IsEmpty() && !WriteFileData(path, file.Data.data(), file.Data.size()))
        AtlMessageBox(m_hWnd, L"Failed to save the image", IDR_MAINFRAME, MB_ICONERROR);
    return 0;
}

// a bitmap resource: a DIB without its file header; anything else WIC can decode is accepted as well
bool CBitmapView::SetData(std::span<const std::byte> data) {
    if (data.size() >= sizeof(BITMAPINFOHEADER)) {
        auto header = (const BITMAPINFOHEADER*)data.data();
        if (header->biSize >= sizeof(BITMAPINFOHEADER) && header->biSize < data.size()) {
            CClientDC dc(m_hWnd);
            m_bmp.Attach(::CreateDIBitmap(dc.m_hDC, header, CBM_INIT, data.data() + header->biSize,
                (const BITMAPINFO*)header, header->biBitCount * header->biPlanes > 8 ? DIB_RGB_COLORS : DIB_PAL_COLORS));
            if (m_bmp) {
                m_Data.assign(data.begin(), data.end());
                m_Dib = true;
                m_Width = header->biWidth;
                m_Height = std::abs(header->biHeight);
                m_Alpha = false;
                SetScrollSize(m_Width, m_Height);
                Frame()->SetStatusText(2, std::format(L"{} x {}", m_Width, m_Height).c_str());
                Invalidate();
                return true;
            }
        }
    }
    return SetImage(data);
}

// PNG, JPEG, GIF, BMP files... (the first frame), with their transparency
bool CBitmapView::SetImage(std::span<const std::byte> data) {
    CComPtr<IWICImagingFactory> spFactory;
    if (FAILED(spFactory.CoCreateInstance(CLSID_WICImagingFactory2)))
        return false;

    CComPtr<IWICStream> spStm;
    if (FAILED(spFactory->CreateStream(&spStm)) || FAILED(spStm->InitializeFromMemory((BYTE*)data.data(), (DWORD)data.size())))
        return false;
    CComPtr<IWICBitmapDecoder> spDecoder;
    if (FAILED(spFactory->CreateDecoderFromStream(spStm, nullptr, WICDecodeMetadataCacheOnLoad, &spDecoder)))
        return false;

    CComPtr<IWICBitmapFrameDecode> spFrame;
    if (FAILED(spDecoder->GetFrame(0, &spFrame)))
        return false;

    // premultiplied alpha, as AlphaBlend wants it
    CComPtr<IWICFormatConverter> spConverter;
    spFactory->CreateFormatConverter(&spConverter);
    if (FAILED(spConverter->Initialize(spFrame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return false;

    UINT width, height;
    spFrame->GetSize(&width, &height);
    BITMAPINFO bminfo = { sizeof(bminfo) };
    bminfo.bmiHeader.biWidth = width;
    bminfo.bmiHeader.biHeight = -(LONG)height;
    bminfo.bmiHeader.biPlanes = 1;
    bminfo.bmiHeader.biBitCount = 32;
    bminfo.bmiHeader.biCompression = BI_RGB;
    void* bits;
    CBitmap bmp(::CreateDIBSection(nullptr, &bminfo, DIB_RGB_COLORS, &bits, nullptr, 0));
    if (!bmp || FAILED(spConverter->CopyPixels(nullptr, width * sizeof(DWORD), width * height * sizeof(DWORD), (BYTE*)bits)))
        return false;

    m_bmp.Attach(bmp.Detach());
    m_Data.assign(data.begin(), data.end());
    m_Dib = false;
    m_Width = width;
    m_Height = height;
    m_Alpha = true;
    SetScrollSize(m_Width, m_Height);
    Frame()->SetStatusText(2, std::format(L"{} x {}", m_Width, m_Height).c_str());
    Invalidate();
    return true;
}

void CBitmapView::DoPaint(CDCHandle dc) {
    if (m_bmp) {
        CDC dcMem;
        dcMem.CreateCompatibleDC(dc);
        dcMem.SelectBitmap(m_bmp);
        if (m_Alpha) {
            BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            dc.AlphaBlend(0, 0, m_Width, m_Height, dcMem, 0, 0, m_Width, m_Height, blend);
        }
        else
            dc.BitBlt(0, 0, m_Width, m_Height, dcMem, 0, 0, SRCCOPY);
    }
}

LRESULT CBitmapView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    return DefWindowProc();
}
