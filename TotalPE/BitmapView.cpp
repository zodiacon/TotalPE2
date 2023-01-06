#include "pch.h"
#include <wincodec.h>
#include "BitmapView.h"

CBitmapView::CBitmapView(IMainFrame* frame, PCWSTR title) : CViewBase(frame), m_Title(title) {
}

CString CBitmapView::GetTitle() const {
    return m_Title;
}

bool CBitmapView::SetData(std::span<const std::byte> data) {
    CComPtr<IWICImagingFactory> spFactory;
    if (FAILED(spFactory.CoCreateInstance(CLSID_WICImagingFactory2)))
        return false;

    auto header = (const BITMAPINFOHEADER*)data.data();
    CClientDC dc(m_hWnd);
    m_bmp.Attach(::CreateDIBitmap(dc.m_hDC, header, CBM_INIT,
        data.data() + (/*header->biSizeImage && header->biBitCount * header->biPlanes > 8 ? (data.size() - header->biSizeImage) :*/ header->biSize),
        (const BITMAPINFO*)header, header->biBitCount * header->biPlanes > 8 ? DIB_RGB_COLORS : DIB_PAL_COLORS));
    if (m_bmp) {
        SetScrollSize(m_Width = header->biWidth, m_Height = header->biHeight);
    }
    else {
        CComPtr<IStream> spStm;
        auto hr = ::CreateStreamOnHGlobal(nullptr, TRUE, &spStm);
        spStm->Write(data.data(), (ULONG)data.size(), nullptr);
        LARGE_INTEGER pos{ 0 };
        spStm->Seek(pos, STREAM_SEEK_SET, nullptr);
        CComPtr<IWICBitmapDecoder> spDecoder;
        hr = spFactory->CreateDecoderFromStream(spStm, nullptr, WICDecodeMetadataCacheOnLoad, &spDecoder);
        if (FAILED(hr))
            return false;

        CComPtr<IWICBitmapFrameDecode> spFrame;
        if (FAILED(spDecoder->GetFrame(0, &spFrame)))
            return false;

        CComQIPtr<IWICBitmapSource> spSrc(spFrame);
        CComPtr<IWICFormatConverter> spConverter;
        spFactory->CreateFormatConverter(&spConverter);
        hr = spConverter->Initialize(spSrc, GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr))
            return false;

        CComQIPtr<IWICBitmapSource> spBitmap(spConverter);
        ATLASSERT(spBitmap);
        spFrame->GetSize(&m_Width, &m_Height);
        SetScrollSize(m_Width, m_Height);

        BITMAPINFO bminfo = { sizeof(bminfo) };
        bminfo.bmiHeader.biWidth = m_Width;
        bminfo.bmiHeader.biHeight = -(LONG)m_Height;
        bminfo.bmiHeader.biPlanes = 1;
        bminfo.bmiHeader.biBitCount = 32;
        bminfo.bmiHeader.biCompression = BI_RGB;
        CDC dc(::GetDC(nullptr));
        void* bits;
        m_bmp.Attach(::CreateDIBSection(dc.m_hDC, &bminfo, DIB_RGB_COLORS, &bits, nullptr, 0));
        spBitmap->CopyPixels(nullptr, m_Width * sizeof(DWORD), m_Width * m_Height * sizeof(DWORD), (BYTE*)bits);
    }
    Frame()->SetStatusText(2, std::format(L"{} x {}", m_Width, m_Height).c_str());

    Invalidate();
    return true;
}

void CBitmapView::DoPaint(CDCHandle dc) {
    if (m_bmp) {
        CDC dcMem;
        dcMem.CreateCompatibleDC(dc);
        dcMem.SelectBitmap(m_bmp);
        dc.BitBlt(0, 0, m_Width, m_Height, dcMem, 0, 0, SRCCOPY);
    }
}

LRESULT CBitmapView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    return DefWindowProc();
}

