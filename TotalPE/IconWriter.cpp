#include "pch.h"
#include "IconWriter.h"

namespace {
    struct ICONDIRENTRY {
        BYTE nWidth;
        BYTE nHeight;
        BYTE nNumColorsInPalette; // 0 if no palette
        BYTE nReserved; // should be 0
        WORD nNumColorPlanes; // 0 or 1
        WORD nBitsPerPixel;
        ULONG nDataLength; // length in bytes
        ULONG nOffset; // offset of BMP or PNG data from beginning of file
    };

    struct ICONHEADER {
        WORD idReserved; // must be 0
        WORD idType; // 1 = ICON, 2 = CURSOR
        WORD idCount; // number of images
    };
}

static bool GetIconData(HICON hIcon, int nColorBits, std::vector<std::byte>& buff, bool icon) {

    // Write header
    ICONHEADER icoHeader{ 0 };
    icoHeader.idCount = 1;
    icoHeader.idType = icon ? 1 : 2;
    auto header = reinterpret_cast<std::byte*>(&icoHeader);
    buff.insert(buff.end(), header, header + sizeof(icoHeader));

    // Get information about icon
    ICONINFO iconInfo;
    ::GetIconInfo(hIcon, &iconInfo);
    BITMAPINFO bmInfo = { 0 };
    bmInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmInfo.bmiHeader.biBitCount = 0;    // don't get the color table     
    HDC dc = CreateCompatibleDC(nullptr);
    if (!::GetDIBits(dc, iconInfo.hbmColor, 0, 0, nullptr, &bmInfo, DIB_RGB_COLORS)) {
        return false;
    }

    // Allocate size of bitmap info header plus space for color table:
    uint32_t nBmInfoSize = sizeof(BITMAPINFOHEADER);
    if (nColorBits < 24) {
        nBmInfoSize += (int)sizeof(RGBQUAD) * (1 << nColorBits);
    }

    std::vector<UCHAR> bitmapInfo;
    bitmapInfo.resize(nBmInfoSize);
    auto pBmInfo = (BITMAPINFO*)bitmapInfo.data();
    memcpy(pBmInfo, &bmInfo, sizeof(BITMAPINFOHEADER));

    // Get bitmap data:
    if (!bmInfo.bmiHeader.biSizeImage)
        return false;
    std::vector<UCHAR> bits;
    bits.resize(bmInfo.bmiHeader.biSizeImage);
    pBmInfo->bmiHeader.biBitCount = (WORD)nColorBits;
    pBmInfo->bmiHeader.biCompression = BI_RGB;
    if (!::GetDIBits(dc, iconInfo.hbmColor, 0, bmInfo.bmiHeader.biHeight, bits.data(), pBmInfo, DIB_RGB_COLORS)) {
        return false;
    }

    // Get mask data
    BITMAPINFO maskInfo = { 0 };
    maskInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    maskInfo.bmiHeader.biBitCount = 0;  // don't get the color table     
    if (!::GetDIBits(dc, iconInfo.hbmMask, 0, 0, nullptr, &maskInfo, DIB_RGB_COLORS) || maskInfo.bmiHeader.biBitCount != 1)
        return false;

    std::vector<UCHAR> maskBits;
    maskBits.resize(maskInfo.bmiHeader.biSizeImage);
    std::vector<UCHAR> maskInfoBytes;
    maskInfoBytes.resize(sizeof(BITMAPINFO) + 2 * sizeof(RGBQUAD));
    auto pMaskInfo = (BITMAPINFO*)maskInfoBytes.data();
    memcpy(pMaskInfo, &maskInfo, sizeof(maskInfo));
    if (!::GetDIBits(dc, iconInfo.hbmMask, 0, maskInfo.bmiHeader.biHeight, maskBits.data(), pMaskInfo, DIB_RGB_COLORS)) {
        return false;
    }

    // Write directory entry:
    ICONDIRENTRY dir;
    dir.nWidth = (UCHAR)pBmInfo->bmiHeader.biWidth;
    dir.nHeight = (UCHAR)pBmInfo->bmiHeader.biHeight;
    dir.nNumColorsInPalette = (nColorBits == 4 ? 16 : 0);
    dir.nReserved = 0;
    dir.nNumColorPlanes = 0;
    dir.nBitsPerPixel = pBmInfo->bmiHeader.biBitCount;
    dir.nDataLength = pBmInfo->bmiHeader.biSizeImage + pMaskInfo->bmiHeader.biSizeImage + nBmInfoSize;
    dir.nOffset = sizeof(dir) + sizeof(icoHeader);
    buff.insert(buff.end(), reinterpret_cast<std::byte*>(&dir), reinterpret_cast<std::byte*>(&dir) + sizeof(dir));

    // Write DIB header (including color table):
    int nBitsSize = pBmInfo->bmiHeader.biSizeImage;
    pBmInfo->bmiHeader.biHeight *= 2; // because the header is for both image and mask
    pBmInfo->bmiHeader.biCompression = 0;
    pBmInfo->bmiHeader.biSizeImage += pMaskInfo->bmiHeader.biSizeImage; // because the header is for both image and mask
    buff.insert(buff.end(), reinterpret_cast<std::byte*>(&pBmInfo->bmiHeader), reinterpret_cast<std::byte*>(&pBmInfo->bmiHeader) + nBmInfoSize);

    // Write image data
    buff.insert(buff.end(), reinterpret_cast<std::byte*>(bits.data()), reinterpret_cast<std::byte*>(bits.data()) + nBitsSize);

    // Write mask data
    buff.insert(buff.end(), reinterpret_cast<std::byte*>(maskBits.data()), reinterpret_cast<std::byte*>(maskBits.data()) + pMaskInfo->bmiHeader.biSizeImage);

    ::DeleteObject(iconInfo.hbmColor);
    ::DeleteObject(iconInfo.hbmMask);

    ::DeleteDC(dc);

    return true;
}

bool IconWriter::Save(PCWSTR path, HICON const hIcon, int colors, bool icon) {
    std::vector<std::byte> data;
    if (!GetIconData(hIcon, colors, data, icon))
        return false;

    auto hFile = ::CreateFile(path, GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD bytes;
    auto ok = ::WriteFile(hFile, data.data(), (DWORD)data.size(), &bytes, nullptr);
    ::CloseHandle(hFile);
    return ok;
}
