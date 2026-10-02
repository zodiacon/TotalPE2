#include "pch.h"
#include "Overlay.h"
#include "PEAnomalies.h"
#include <PEFile.h>
#include "Authenticode.h"
#include <bcrypt.h>

OverlayInfo FindOverlay(PEFile const& pe) {
	OverlayInfo info;
	auto nt = pe.GetNTHeader();
	auto sections = pe.GetSecHeaders();
	if (!nt || !sections || pe.GetFileSize() == 0)
		return info;

	const uint64_t fileSize = pe.GetFileSize();
	// the headers and the raw data of the sections
	uint64_t dataEnd = pe.GetFileInfo()->IsPE64 ? nt->NTHdr64.OptionalHeader.SizeOfHeaders : nt->NTHdr32.OptionalHeader.SizeOfHeaders;
	for (auto const& s : *sections)
		if (s.SecHdr.SizeOfRawData)
			dataEnd = std::max<uint64_t>(dataEnd, (uint64_t)s.SecHdr.PointerToRawData + s.SecHdr.SizeOfRawData);
	if (dataEnd >= fileSize)
		return info;

	uint64_t start = dataEnd, end = fileSize;
	if (auto dirs = pe.GetDataDirs(); dirs && dirs->size() > IMAGE_DIRECTORY_ENTRY_SECURITY) {
		auto const& cert = (*dirs)[IMAGE_DIRECTORY_ENTRY_SECURITY].DataDir;
		if (cert.Size && cert.VirtualAddress >= dataEnd && (uint64_t)cert.VirtualAddress + cert.Size <= fileSize) {
			info.CertificateOffset = cert.VirtualAddress;
			info.CertificateSize = cert.Size;
			// the table sits in front of the overlay or behind it; in the middle it splits the data in two, and the first part counts
			if (cert.VirtualAddress == start)
				start = (uint64_t)cert.VirtualAddress + cert.Size;
			else
				end = cert.VirtualAddress;
		}
	}
	if (end <= start)
		return info;

	info.Offset = start;
	info.Size = end - start;
	auto data = pe.GetSpan((uint32_t)start, (uint32_t)std::min<uint64_t>(info.Size, 0xFFFFFFFF));
	info.Entropy = ComputeEntropy((const uint8_t*)data.data(), data.size());
	info.Kind = IdentifyData(data);
	return info;
}

namespace {
	bool Starts(std::span<const std::byte> d, std::initializer_list<uint8_t> magic, size_t at = 0) {
		if (d.size() < at + magic.size())
			return false;
		size_t i = at;
		for (auto b : magic)
			if (std::to_integer<uint8_t>(d[i++]) != b)
				return false;
		return true;
	}
}

std::wstring IdentifyData(std::span<const std::byte> d) {
	if (d.size() < 2)
		return L"";

	if (Starts(d, { 'P', 'K', 3, 4 }))
		return L"ZIP archive";
	if (Starts(d, { 'P', 'K', 5, 6 }))
		return L"ZIP archive (empty)";
	if (Starts(d, { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C }))
		return L"7-Zip archive";
	if (Starts(d, { 'R', 'a', 'r', '!', 0x1A, 0x07 }))
		return L"RAR archive";
	if (Starts(d, { 'M', 'S', 'C', 'F' }))
		return L"Cabinet (CAB) archive";
	if (Starts(d, { 0x1F, 0x8B }))
		return L"gzip data";
	if (Starts(d, { 'B', 'Z', 'h' }))
		return L"bzip2 data";
	if (Starts(d, { 0xFD, '7', 'z', 'X', 'Z', 0 }))
		return L"XZ data";
	if (Starts(d, { 0x28, 0xB5, 0x2F, 0xFD }))
		return L"Zstandard data";
	// the NSIS installer: flags, the magic 0xDEADBEEF and "NullsoftInst"
	if (Starts(d, { 0xEF, 0xBE, 0xAD, 0xDE, 'N', 'u', 'l', 'l', 's', 'o', 'f', 't', 'I', 'n', 's', 't' }, 4))
		return L"NSIS installer data";
	if (Starts(d, { 'M', 'Z' })) {
		if (d.size() >= 0x40) {
			uint32_t lfanew = 0;
			memcpy(&lfanew, d.data() + 0x3C, 4);
			if (lfanew <= d.size() - 4 && Starts(d, { 'P', 'E', 0, 0 }, lfanew))
				return L"PE file";
		}
		return L"MZ executable";
	}
	if (Starts(d, { '%', 'P', 'D', 'F', '-' }))
		return L"PDF document";
	if (Starts(d, { 0x89, 'P', 'N', 'G' }))
		return L"PNG image";
	if (Starts(d, { 'G', 'I', 'F', '8' }))
		return L"GIF image";
	if (Starts(d, { 0xFF, 0xD8, 0xFF }))
		return L"JPEG image";
	if (Starts(d, { 0x30, 0x82 }))
		return L"ASN.1 data (a certificate or a signature, perhaps)";
	if (Starts(d, { '<', '?', 'x', 'm', 'l' }))
		return L"XML text";

	// text: printable ASCII in the first part
	size_t n = std::min<size_t>(d.size(), 256);
	bool text = true;
	for (size_t i = 0; i < n && text; i++) {
		auto c = std::to_integer<uint8_t>(d[i]);
		text = (c >= 0x20 && c < 0x7F) || c == '\r' || c == '\n' || c == '\t';
	}
	if (text && n >= 8)
		return L"Text";
	return L"";
}

std::string Sha256Hex(std::span<const std::byte> data) {
	std::string result;
	for (auto b : HashBytes(BCRYPT_SHA256_ALGORITHM, data))
		result += std::format("{:02x}", b);
	return result;
}
