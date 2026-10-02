#include "pch.h"
#include "Authenticode.h"
#include <PEFile.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <softpub.h>
#include <wintrust.h>
#include <optional>

#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "Wintrust.lib")

#ifndef szOID_RFC3161_counterSign
#define szOID_RFC3161_counterSign "1.3.6.1.4.1.311.3.3.1"
#endif
#ifndef szOID_NESTED_SIGNATURE
#define szOID_NESTED_SIGNATURE "1.3.6.1.4.1.311.2.4.1"
#endif

//
// hashing
//

namespace {
	class Hasher {
	public:
		explicit Hasher(PCWSTR algorithm) {
			if (BCryptOpenAlgorithmProvider(&m_Alg, algorithm, nullptr, 0) != 0)
				return;
			if (BCryptCreateHash(m_Alg, &m_Hash, nullptr, 0, nullptr, 0, 0) == 0)
				m_Ok = true;
		}
		~Hasher() {
			if (m_Hash)
				BCryptDestroyHash(m_Hash);
			if (m_Alg)
				BCryptCloseAlgorithmProvider(m_Alg, 0);
		}
		Hasher(Hasher const&) = delete;
		Hasher& operator=(Hasher const&) = delete;

		bool Ok() const { return m_Ok; }

		void Update(std::span<const std::byte> data) {
			// the API takes 32 bit sizes
			while (m_Ok && !data.empty()) {
				auto chunk = (ULONG)std::min<size_t>(data.size(), 1u << 30);
				m_Ok = BCryptHashData(m_Hash, (PUCHAR)data.data(), chunk, 0) == 0;
				data = data.subspan(chunk);
			}
		}

		std::vector<uint8_t> Finish() {
			if (!m_Ok)
				return {};
			ULONG size = 0, got = 0;
			if (BCryptGetProperty(m_Alg, BCRYPT_HASH_LENGTH, (PUCHAR)&size, sizeof(size), &got, 0) != 0)
				return {};
			std::vector<uint8_t> digest(size);
			if (BCryptFinishHash(m_Hash, digest.data(), size, 0) != 0)
				return {};
			return digest;
		}

	private:
		BCRYPT_ALG_HANDLE m_Alg{ nullptr };
		BCRYPT_HASH_HANDLE m_Hash{ nullptr };
		bool m_Ok{ false };
	};
}

std::vector<uint8_t> HashBytes(PCWSTR algorithm, std::span<const std::byte> data) {
	Hasher h(algorithm);
	h.Update(data);
	return h.Finish();
}

std::wstring BytesToHexString(std::span<const uint8_t> bytes) {
	std::wstring s;
	s.reserve(bytes.size() * 2);
	for (auto b : bytes)
		s += std::format(L"{:02X}", b);
	return s;
}

std::vector<uint8_t> ComputeAuthenticodeDigest(PEFile const& pe, PCWSTR algorithm) {
	auto nt = pe.GetNTHeader();
	auto sections = pe.GetSecHeaders();
	auto info = pe.GetFileInfo();
	if (!nt || !sections || !info)
		return {};

	const uint64_t fileSize = pe.GetFileSize();
	const bool is64 = info->IsPE64;
	auto data = pe.GetSpan(0, (uint32_t)fileSize);
	if (data.size() < 0x40)
		return {};
	uint32_t lfanew = 0;
	memcpy(&lfanew, data.data() + 0x3C, 4);

	// the optional header starts after the signature and the file header; the checksum is at offset 64 of it, and
	// the data directories at 96 (PE32) or 112 (PE32+)
	const uint64_t optional = (uint64_t)lfanew + 4 + sizeof(IMAGE_FILE_HEADER);
	const uint64_t checksumOffset = optional + 64;
	const uint64_t dirsOffset = optional + (is64 ? 112 : 96);
	const uint64_t certEntryOffset = dirsOffset + IMAGE_DIRECTORY_ENTRY_SECURITY * sizeof(IMAGE_DATA_DIRECTORY);
	const uint64_t sizeOfHeaders = is64 ? nt->NTHdr64.OptionalHeader.SizeOfHeaders : nt->NTHdr32.OptionalHeader.SizeOfHeaders;
	if (certEntryOffset + 8 > sizeOfHeaders || sizeOfHeaders > fileSize)
		return {};

	uint32_t certSize = 0;
	if (auto dirs = pe.GetDataDirs(); dirs && dirs->size() > IMAGE_DIRECTORY_ENTRY_SECURITY)
		certSize = (*dirs)[IMAGE_DIRECTORY_ENTRY_SECURITY].DataDir.Size;

	Hasher hasher(algorithm);
	if (!hasher.Ok())
		return {};
	auto range = [&](uint64_t from, uint64_t to) {
		if (to > from && from < data.size())
			hasher.Update(data.subspan((size_t)from, (size_t)(std::min<uint64_t>(to, data.size()) - from)));
	};
	range(0, checksumOffset);
	range(checksumOffset + 4, certEntryOffset);
	range(certEntryOffset + 8, sizeOfHeaders);
	uint64_t hashed = sizeOfHeaders;

	// the sections in the order of their position in the file
	std::vector<IMAGE_SECTION_HEADER const*> ordered;
	for (auto const& s : *sections)
		if (s.SecHdr.SizeOfRawData)
			ordered.push_back(&s.SecHdr);
	std::sort(ordered.begin(), ordered.end(), [](auto a, auto b) { return a->PointerToRawData < b->PointerToRawData; });
	for (auto h : ordered) {
		range(h->PointerToRawData, (uint64_t)h->PointerToRawData + h->SizeOfRawData);
		hashed += h->SizeOfRawData;
	}

	// whatever follows, except the certificate table
	if (fileSize > hashed + certSize)
		range(hashed, fileSize - certSize);
	return hasher.Finish();
}

//
// DER
//

namespace {
	struct Tlv {
		uint8_t Tag{ 0 };
		std::span<const std::byte> Content;
		size_t Total{ 0 };	// the size of the tag, the length and the content
	};

	std::optional<Tlv> ReadTlv(std::span<const std::byte> d) {
		if (d.size() < 2)
			return std::nullopt;
		Tlv t;
		t.Tag = std::to_integer<uint8_t>(d[0]);
		size_t length = std::to_integer<uint8_t>(d[1]);
		size_t header = 2;
		if (length & 0x80) {
			auto n = length & 0x7F;
			if (n == 0 || n > 4 || d.size() < 2 + n)
				return std::nullopt;	// indefinite lengths do not occur in the DER of Authenticode
			length = 0;
			for (size_t i = 0; i < n; i++)
				length = length << 8 | std::to_integer<uint8_t>(d[2 + i]);
			header += n;
		}
		if (length > d.size() - header)
			return std::nullopt;
		t.Content = d.subspan(header, length);
		t.Total = header + length;
		return t;
	}

	std::string DecodeOid(std::span<const std::byte> d) {
		if (d.empty())
			return "";
		std::string oid;
		uint64_t value = 0;
		bool first = true;
		for (auto b : d) {
			auto v = std::to_integer<uint8_t>(b);
			value = value << 7 | (v & 0x7F);
			if (v & 0x80)
				continue;
			if (first) {
				auto a = value < 80 ? value / 40 : 2;
				oid = std::format("{}.{}", a, value - a * 40);
				first = false;
			}
			else
				oid += std::format(".{}", value);
			value = 0;
		}
		return oid;
	}
}

bool ParseSpcIndirectData(std::span<const std::byte> content, std::string& algorithmOid, std::vector<uint8_t>& digest) {
	auto outer = ReadTlv(content);
	if (!outer || outer->Tag != 0x30)
		return false;
	// { SpcAttributeTypeAndOptionalValue, DigestInfo }
	auto data = ReadTlv(outer->Content);
	if (!data)
		return false;
	auto digestInfo = ReadTlv(outer->Content.subspan(data->Total));
	if (!digestInfo || digestInfo->Tag != 0x30)
		return false;
	// DigestInfo: { AlgorithmIdentifier, OCTET STRING }
	auto algorithm = ReadTlv(digestInfo->Content);
	if (!algorithm || algorithm->Tag != 0x30)
		return false;
	auto oid = ReadTlv(algorithm->Content);
	if (!oid || oid->Tag != 0x06)
		return false;
	auto octets = ReadTlv(digestInfo->Content.subspan(algorithm->Total));
	if (!octets || octets->Tag != 0x04)
		return false;
	algorithmOid = DecodeOid(oid->Content);
	digest.resize(octets->Content.size());
	memcpy(digest.data(), octets->Content.data(), digest.size());
	return true;
}

bool ParseTimestampInfo(std::span<const std::byte> content, FILETIME& time) {
	auto outer = ReadTlv(content);
	if (!outer || outer->Tag != 0x30)
		return false;
	// TSTInfo: version, policy, message imprint, serial number, then the time as a GeneralizedTime ("20240131120000Z")
	auto rest = outer->Content;
	for (int i = 0; i < 8 && !rest.empty(); i++) {
		auto t = ReadTlv(rest);
		if (!t)
			return false;
		if (t->Tag == 0x18) {
			auto text = t->Content;
			if (text.size() < 14)
				return false;
			int v[6];
			auto num = [&](size_t at, size_t len) {
				int n = 0;
				for (size_t j = 0; j < len; j++) {
					int c = std::to_integer<int>(text[at + j]) - '0';
					if (c < 0 || c > 9)
						return -1;
					n = n * 10 + c;
				}
				return n;
			};
			v[0] = num(0, 4); v[1] = num(4, 2); v[2] = num(6, 2); v[3] = num(8, 2); v[4] = num(10, 2); v[5] = num(12, 2);
			for (int n : v)
				if (n < 0)
					return false;
			SYSTEMTIME st{};
			st.wYear = (WORD)v[0]; st.wMonth = (WORD)v[1]; st.wDay = (WORD)v[2];
			st.wHour = (WORD)v[3]; st.wMinute = (WORD)v[4]; st.wSecond = (WORD)v[5];
			return ::SystemTimeToFileTime(&st, &time) != FALSE;
		}
		rest = rest.subspan(t->Total);
	}
	return false;
}

std::wstring DigestAlgorithmName(std::string const& oid) {
	if (oid == "1.3.14.3.2.26") return L"SHA-1";
	if (oid == "2.16.840.1.101.3.4.2.1") return L"SHA-256";
	if (oid == "2.16.840.1.101.3.4.2.2") return L"SHA-384";
	if (oid == "2.16.840.1.101.3.4.2.3") return L"SHA-512";
	if (oid == "1.2.840.113549.2.5") return L"MD5";
	return L"";
}

PCWSTR DigestAlgorithmCng(std::string const& oid) {
	if (oid == "1.3.14.3.2.26") return BCRYPT_SHA1_ALGORITHM;
	if (oid == "2.16.840.1.101.3.4.2.1") return BCRYPT_SHA256_ALGORITHM;
	if (oid == "2.16.840.1.101.3.4.2.2") return BCRYPT_SHA384_ALGORITHM;
	if (oid == "2.16.840.1.101.3.4.2.3") return BCRYPT_SHA512_ALGORITHM;
	if (oid == "1.2.840.113549.2.5") return BCRYPT_MD5_ALGORITHM;
	return nullptr;
}

//
// the signature
//

namespace {
	std::wstring Widen(const char* s) {
		if (!s || !*s)
			return L"";
		return std::wstring(s, s + strlen(s));
	}

	// decoded PKCS#7 handles
	struct Message {
		HCERTSTORE Store{ nullptr };
		HCRYPTMSG Msg{ nullptr };
		Message() = default;
		Message(Message const&) = delete;
		Message& operator=(Message const&) = delete;
		~Message() {
			if (Msg)
				CryptMsgClose(Msg);
			if (Store)
				CertCloseStore(Store, 0);
		}
		bool Open(std::span<const std::byte> blob) {
			CERT_BLOB b{ (DWORD)blob.size(), (BYTE*)blob.data() };
			DWORD encoding, type, format;
			return CryptQueryObject(CERT_QUERY_OBJECT_BLOB, &b, CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED, CERT_QUERY_FORMAT_FLAG_BINARY,
				0, &encoding, &type, &format, &Store, &Msg, nullptr) != FALSE;
		}
		std::vector<BYTE> Param(DWORD param, DWORD index = 0) const {
			DWORD size = 0;
			if (!CryptMsgGetParam(Msg, param, index, nullptr, &size))
				return {};
			std::vector<BYTE> buffer(size);
			if (!CryptMsgGetParam(Msg, param, index, buffer.data(), &size))
				return {};
			buffer.resize(size);
			return buffer;
		}
	};

	std::wstring NameOf(PCCERT_CONTEXT cert, DWORD flags = 0) {
		WCHAR name[512]{};
		CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, flags, nullptr, name, _countof(name));
		return name;
	}

	CertificateSummary Summarize(PCCERT_CONTEXT cert) {
		CertificateSummary c;
		auto info = cert->pCertInfo;
		c.Subject = NameOf(cert);
		c.Issuer = NameOf(cert, CERT_NAME_ISSUER_FLAG);
		c.NotBefore = info->NotBefore;
		c.NotAfter = info->NotAfter;
		// the serial number is stored with the least significant byte first
		for (DWORD i = info->SerialNumber.cbData; i > 0; i--)
			c.SerialNumber += std::format(L"{:02X}", info->SerialNumber.pbData[i - 1]);
		BYTE thumb[20];
		DWORD size = sizeof(thumb);
		if (CertGetCertificateContextProperty(cert, CERT_SHA1_HASH_PROP_ID, thumb, &size))
			c.Thumbprint = BytesToHexString({ thumb, size });
		if (auto oid = CryptFindOIDInfo(CRYPT_OID_INFO_OID_KEY, info->SignatureAlgorithm.pszObjId, 0); oid && oid->pwszName)
			c.SignatureAlgorithm = oid->pwszName;
		else
			c.SignatureAlgorithm = Widen(info->SignatureAlgorithm.pszObjId);
		return c;
	}

	// the name of the certificate that signed a message
	std::wstring SignerName(Message const& m, DWORD index = 0) {
		auto buffer = m.Param(CMSG_SIGNER_INFO_PARAM, index);
		if (buffer.empty())
			return L"";
		auto si = reinterpret_cast<CMSG_SIGNER_INFO*>(buffer.data());
		CERT_INFO ci{};
		ci.Issuer = si->Issuer;
		ci.SerialNumber = si->SerialNumber;
		if (auto cert = CertFindCertificateInStore(m.Store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_CERT, &ci, nullptr)) {
			auto name = NameOf(cert);
			CertFreeCertificateContext(cert);
			return name;
		}
		return L"";
	}

	void ReadTimestamps(CMSG_SIGNER_INFO const& si, Message const& m, SignatureSummary& sig) {
		for (DWORD i = 0; i < si.UnauthAttrs.cAttr; i++) {
			auto const& attr = si.UnauthAttrs.rgAttr[i];
			if (attr.cValue == 0 || attr.pszObjId == nullptr)
				continue;
			auto const& value = attr.rgValue[0];

			if (strcmp(attr.pszObjId, szOID_RSA_counterSign) == 0) {
				// the old format: the signer info of the time stamping authority, with the time among its attributes
				BYTE* decoded = nullptr;
				DWORD size = 0;
				if (!CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS7_SIGNER_INFO, value.pbData, value.cbData,
					CRYPT_DECODE_ALLOC_FLAG, nullptr, &decoded, &size))
					continue;
				auto csi = reinterpret_cast<CMSG_SIGNER_INFO*>(decoded);
				for (DWORD j = 0; j < csi->AuthAttrs.cAttr; j++) {
					auto const& a = csi->AuthAttrs.rgAttr[j];
					if (strcmp(a.pszObjId, szOID_RSA_signingTime) != 0 || a.cValue == 0)
						continue;
					FILETIME ft{};
					DWORD ftSize = sizeof(ft);
					if (CryptDecodeObject(X509_ASN_ENCODING, szOID_RSA_signingTime, a.rgValue[0].pbData, a.rgValue[0].cbData, 0, &ft, &ftSize)) {
						TimestampSummary t;
						t.Kind = L"Counter-signature";
						t.Time = ft;
						CERT_INFO ci{};
						ci.Issuer = csi->Issuer;
						ci.SerialNumber = csi->SerialNumber;
						if (auto cert = CertFindCertificateInStore(m.Store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_CERT, &ci, nullptr)) {
							t.Signer = NameOf(cert);
							CertFreeCertificateContext(cert);
						}
						sig.Timestamps.push_back(std::move(t));
					}
					break;
				}
				LocalFree(decoded);
			}
			else if (strcmp(attr.pszObjId, szOID_RFC3161_counterSign) == 0) {
				// a time stamp token: SignedData whose content is the time
				Message token;
				if (!token.Open({ (const std::byte*)value.pbData, value.cbData }))
					continue;
				auto content = token.Param(CMSG_CONTENT_PARAM);
				TimestampSummary t;
				t.Kind = L"RFC 3161";
				if (!ParseTimestampInfo({ (const std::byte*)content.data(), content.size() }, t.Time))
					continue;
				t.Signer = SignerName(token);
				sig.Timestamps.push_back(std::move(t));
			}
		}
	}
}

SignatureSummary AnalyzeSignature(std::span<const std::byte> pkcs7, PEFile const* pe) {
	SignatureSummary sig;
	Message m;
	if (pkcs7.empty() || !m.Open(pkcs7)) {
		sig.Error = L"The data is not PKCS#7 SignedData";
		return sig;
	}
	sig.Decoded = true;

	// what the signature covers: the algorithm and the hash
	auto content = m.Param(CMSG_CONTENT_PARAM);
	std::string oid;
	if (ParseSpcIndirectData({ (const std::byte*)content.data(), content.size() }, oid, sig.SignedDigest)) {
		sig.DigestAlgorithm = DigestAlgorithmName(oid);
		if (sig.DigestAlgorithm.empty())
			sig.DigestAlgorithm = Widen(oid.c_str());
		if (pe)
			if (auto cng = DigestAlgorithmCng(oid))
				sig.ComputedDigest = ComputeAuthenticodeDigest(*pe, cng);
	}
	else {
		sig.Error = L"The content of the signature is not an Authenticode hash";
	}

	// the certificates
	for (PCCERT_CONTEXT cert = nullptr; (cert = CertEnumCertificatesInStore(m.Store, cert)) != nullptr;)
		sig.Certificates.push_back(Summarize(cert));

	auto signerBuffer = m.Param(CMSG_SIGNER_INFO_PARAM);
	if (!signerBuffer.empty()) {
		auto si = reinterpret_cast<CMSG_SIGNER_INFO*>(signerBuffer.data());
		CERT_INFO ci{};
		ci.Issuer = si->Issuer;
		ci.SerialNumber = si->SerialNumber;
		if (auto cert = CertFindCertificateInStore(m.Store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_CERT, &ci, nullptr)) {
			sig.Signer = Summarize(cert);
			sig.HasSigner = true;
			CertFreeCertificateContext(cert);
		}
		ReadTimestamps(*si, m, sig);

		// signatures nested in this one
		for (DWORD i = 0; i < si->UnauthAttrs.cAttr; i++) {
			auto const& attr = si->UnauthAttrs.rgAttr[i];
			if (attr.pszObjId && strcmp(attr.pszObjId, szOID_NESTED_SIGNATURE) == 0)
				for (DWORD v = 0; v < attr.cValue; v++)
					sig.Nested.push_back(AnalyzeSignature({ (const std::byte*)attr.rgValue[v].pbData, attr.rgValue[v].cbData }, pe));
		}
	}
	return sig;
}

//
// trust
//

long VerifyFileTrust(std::wstring const& path, std::wstring& message) {
	WINTRUST_FILE_INFO file{ sizeof(file) };
	file.pcwszFilePath = path.c_str();
	WINTRUST_DATA data{ sizeof(data) };
	data.dwUIChoice = WTD_UI_NONE;
	data.fdwRevocationChecks = WTD_REVOKE_NONE;
	data.dwUnionChoice = WTD_CHOICE_FILE;
	data.pFile = &file;
	data.dwStateAction = WTD_STATEACTION_VERIFY;
	data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_NONE;
	GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
	auto result = ::WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &data);
	data.dwStateAction = WTD_STATEACTION_CLOSE;
	::WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &data);

	switch ((uint32_t)result) {
		case 0: message = L"The signature is valid and trusted"; break;
		case 0x800B0100: message = L"The file is not signed"; break;
		case 0x80096010: message = L"The file was changed after it was signed (the hash does not match)"; break;
		case 0x800B0109: message = L"The certificate chain ends in a root that is not trusted"; break;
		case 0x800B010A: message = L"The certificate chain could not be built"; break;
		case 0x800B0101: message = L"A certificate in the chain has expired (and the signature is not time stamped)"; break;
		case 0x800B0111: message = L"The publisher is explicitly distrusted"; break;
		case 0x800B0004: message = L"The subject is not trusted for the requested action"; break;
		case 0x80092026: message = L"The signature is not allowed by the security settings"; break;
		default: {
			WCHAR text[256]{};
			if (!::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, (DWORD)result, 0, text, _countof(text), nullptr))
				text[0] = 0;
			std::wstring t = text;
			while (!t.empty() && (t.back() == L'\r' || t.back() == L'\n' || t.back() == L' '))
				t.pop_back();
			message = t.empty() ? L"Verification failed" : t;
			break;
		}
	}
	return result;
}

AuthenticodeResult AnalyzeAuthenticode(PEFile const& pe, bool verifyTrust) {
	AuthenticodeResult result;
	if (auto entries = pe.GetSecurity()) {
		for (auto const& e : *entries) {
			if (e.WinCert.wCertificateType != WIN_CERT_TYPE_PKCS_SIGNED_DATA) {
				SignatureSummary s;
				s.Error = L"Not an Authenticode (PKCS#7) signature";
				result.Signatures.push_back(std::move(s));
				continue;
			}
			result.Signatures.push_back(AnalyzeSignature({ (const std::byte*)e.CertData.data(), e.CertData.size() }, &pe));
		}
	}
	if (verifyTrust && !result.Signatures.empty() && !pe.GetPath().empty())
		result.TrustCode = VerifyFileTrust(pe.GetPath(), result.Trust);
	return result;
}

//
// for the views
//

namespace {
	std::wstring FormatTime(FILETIME const& ft) {
		FILETIME local{};
		SYSTEMTIME st{};
		if (!::FileTimeToLocalFileTime(&ft, &local) || !::FileTimeToSystemTime(&local, &st))
			return L"";
		WCHAR date[64]{}, time[64]{};
		::GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, _countof(date));
		::GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, time, _countof(time));
		return std::wstring(date) + L"  " + time;
	}
}

std::vector<std::pair<std::wstring, std::wstring>> DescribeSignature(SignatureSummary const& sig, std::wstring const& prefix) {
	std::vector<std::pair<std::wstring, std::wstring>> rows;
	auto add = [&](std::wstring name, std::wstring value) { rows.push_back({ prefix + name, std::move(value) }); };

	if (!sig.Decoded) {
		add(L"Note", sig.Error.empty() ? L"The signature could not be decoded" : sig.Error);
		return rows;
	}

	if (!sig.DigestAlgorithm.empty())
		add(L"Digest Algorithm", sig.DigestAlgorithm);
	if (!sig.SignedDigest.empty())
		add(L"Digest in Signature", BytesToHexString(sig.SignedDigest));
	if (!sig.ComputedDigest.empty())
		add(L"Digest of File", BytesToHexString(sig.ComputedDigest));
	if (!sig.SignedDigest.empty() && !sig.ComputedDigest.empty())
		add(L"Digest Check", sig.DigestMatches() ? L"The digests match" : L"The digests DO NOT match: the file was changed after it was signed");
	if (!sig.Error.empty())
		add(L"Note", sig.Error);

	if (sig.HasSigner) {
		add(L"Subject", sig.Signer.Subject);
		add(L"Issuer", sig.Signer.Issuer);
		add(L"Valid From", FormatTime(sig.Signer.NotBefore));
		add(L"Valid To", FormatTime(sig.Signer.NotAfter));
		add(L"Serial Number", sig.Signer.SerialNumber);
		add(L"Thumbprint (SHA-1)", sig.Signer.Thumbprint);
		add(L"Signature Algorithm", sig.Signer.SignatureAlgorithm);
	}
	else {
		add(L"Note", L"The signing certificate is not in the signature");
	}

	for (auto const& t : sig.Timestamps)
		add(L"Timestamp (" + t.Kind + L")", FormatTime(t.Time) + (t.Signer.empty() ? L"" : L"  by " + t.Signer));

	int n = 1;
	for (auto const& c : sig.Certificates)
		add(std::format(L"Certificate {}", n++), c.Subject + L"  (issued by " + c.Issuer + L", valid " + FormatTime(c.NotBefore) + L" to " + FormatTime(c.NotAfter) + L")");

	n = 1;
	for (auto const& nested : sig.Nested) {
		auto sub = DescribeSignature(nested, prefix + std::format(L"Nested {}: ", n++));
		rows.insert(rows.end(), sub.begin(), sub.end());
	}
	return rows;
}
