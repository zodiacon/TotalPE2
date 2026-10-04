#pragma once

#include <cstdint>
#include <span>
#include <string>

class PEFile;

// The data that follows the last section of a PE file (installers, archives and signatures are often appended there).
// The certificate table is also stored after the sections, but it is not part of the overlay.
struct OverlayInfo {
	uint64_t Offset{ 0 };	// file offset
	uint64_t Size{ 0 };		// 0 if the file has no overlay
	uint64_t CertificateOffset{ 0 }, CertificateSize{ 0 };	// the certificate table, if it is in the file
	std::wstring Kind;		// what the data looks like ("ZIP archive"), empty if it is not recognized
	double Entropy{ 0 };	// 0 to 8; high values mean compressed or encrypted data

	bool Empty() const { return Size == 0; }
};

OverlayInfo FindOverlay(PEFile const& pe);

// Recognizes data by its first bytes (archives, installers, executables, images). Empty if it is not recognized.
std::wstring IdentifyData(std::span<const std::byte> data);

// "7a3f..." for the bytes
std::string Sha256Hex(std::span<const std::byte> data);
std::string Sha1Hex(std::span<const std::byte> data);
