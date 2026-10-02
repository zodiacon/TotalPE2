#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <Windows.h>

class PEFile;

// Authenticode: the digital signatures of PE files. A signature is PKCS#7 SignedData in the certificate table, whose content
// is the hash of the file. The hash leaves out the fields that signing changes (the checksum and the certificate table entry).

struct CertificateSummary {
	std::wstring Subject, Issuer, SerialNumber, Thumbprint, SignatureAlgorithm;
	FILETIME NotBefore{}, NotAfter{};
};

struct TimestampSummary {
	std::wstring Kind;		// "Counter-signature" (the old format) or "RFC 3161"
	FILETIME Time{};
	std::wstring Signer;	// the time stamping authority
};

struct SignatureSummary {
	bool Decoded{ false };
	std::wstring Error;				// why the signature could not be read
	std::wstring DigestAlgorithm;	// "SHA-256"
	std::vector<uint8_t> SignedDigest;		// the hash of the file that the signature contains
	std::vector<uint8_t> ComputedDigest;	// the hash of the file as it is now (empty if the algorithm is not known)
	bool HasSigner{ false };
	CertificateSummary Signer;
	std::vector<CertificateSummary> Certificates;	// everything in the signature: the signer, intermediate and root authorities
	std::vector<TimestampSummary> Timestamps;
	std::vector<SignatureSummary> Nested;			// more signatures in the same entry (a file signed with SHA-1 and SHA-256)

	bool DigestMatches() const { return !SignedDigest.empty() && SignedDigest == ComputedDigest; }
};

struct AuthenticodeResult {
	std::vector<SignatureSummary> Signatures;	// one for each entry of the certificate table
	std::wstring Trust;		// the verdict of the system about the file ("The signature is valid and trusted"); empty if not checked
	long TrustCode{ 0 };	// the HRESULT behind it
};

// Decodes a signature (the data of a WIN_CERTIFICATE entry). With 'pe', the hash of the file is computed to compare it with the one in the signature.
SignatureSummary AnalyzeSignature(std::span<const std::byte> pkcs7, PEFile const* pe);

// All the signatures of a file. 'verifyTrust' also asks the system (WinVerifyTrust) if the signature is valid and trusted, which needs the file on disk.
AuthenticodeResult AnalyzeAuthenticode(PEFile const& pe, bool verifyTrust = true);

// Asks the system if the signature of the file is valid. Returns the HRESULT.
long VerifyFileTrust(std::wstring const& path, std::wstring& message);

// The Authenticode hash of a file: of everything except the checksum, the certificate table entry and the certificate table.
// 'algorithm' is the name of a CNG algorithm ("SHA256"). Empty if the file cannot be hashed.
std::vector<uint8_t> ComputeAuthenticodeDigest(PEFile const& pe, PCWSTR algorithm);

// Hash of bytes with a CNG algorithm ("SHA256", "SHA1", "MD5"...). Empty on failure.
std::vector<uint8_t> HashBytes(PCWSTR algorithm, std::span<const std::byte> data);

std::wstring BytesToHexString(std::span<const uint8_t> bytes);

// The SpcIndirectDataContent of a signature: which algorithm was used, and the hash. False if the data is not that structure.
bool ParseSpcIndirectData(std::span<const std::byte> content, std::string& algorithmOid, std::vector<uint8_t>& digest);

// The time of a time stamp token (the content of an RFC 3161 token, TSTInfo)
bool ParseTimestampInfo(std::span<const std::byte> content, FILETIME& time);

// A name for the OID of a hash algorithm ("SHA-256"), and the CNG name that computes it. Empty if unknown.
std::wstring DigestAlgorithmName(std::string const& oid);
PCWSTR DigestAlgorithmCng(std::string const& oid);

// The signature as rows for a list: ("Digest Algorithm", "SHA-256"). 'prefix' goes in front of every name.
std::vector<std::pair<std::wstring, std::wstring>> DescribeSignature(SignatureSummary const& sig, std::wstring const& prefix = L"");
