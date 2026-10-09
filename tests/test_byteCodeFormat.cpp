// =============================================================================
// OES Enterprise — the bytecode format version is the cache's engine half
//
// The cache key used to be GetBuildStamp(), which is __DATE__/__TIME__ of
// core/build.cpp. core is a separate library and is not rebuilt when the
// compiler or the interpreter changes, so a stale blob kept its key.
//
// The key is now kAOTFormatVersion (compiler/byteCodeFormat.h) plus the
// configuration digest. This file is what makes an opcode change unable to
// land without moving that number: the fingerprint is computed from
// codeDef.h, not typed in by hand. When it differs from the fingerprint
// recorded with the current version, the test fails and the message says
// to bump the version.
// =============================================================================

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "backend/compiler/byteCodeFormat.h"
#include "backend/compiler/cache/byteCodeCache.h"
#include "backend/utils/md5.hpp"

#include <wx/filename.h>
#include <wx/textfile.h>

namespace {

// The opcode list as it was when kAOTFormatVersion was last set. Computed
// by OpcodeFingerprint(), which reads codeDef.h: every OPER_ enumerator and
// the TYPE_DELTA macros, comments stripped. 35 still contains OPER_ITER.
//
// When the ternary short-circuit removes OPER_ITER it also sets the version
// to 36. Both have to move together here: a 35 fingerprint on a 36 engine,
// or a 36 engine still advertising 35, would let the cache serve a blob
// that ran both branches of ?().
constexpr std::uint16_t kVersionOfThisFingerprint = 35;
constexpr std::uint64_t kOpcodeFingerprintAtThatVersion = 7984000650070154738ULL;

wxString CodeDefPath()
{
	wxFileName codeDef(wxString::FromUTF8(__FILE__));
	codeDef.RemoveLastDir();   // tests/ -> the repository root
	codeDef.AppendDir(wxT("src"));
	codeDef.AppendDir(wxT("engine"));
	codeDef.AppendDir(wxT("backend"));
	codeDef.AppendDir(wxT("compiler"));
	codeDef.SetFullName(wxT("codeDef.h"));
	codeDef.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
	return codeDef.GetFullPath();
}

// One line of the opcode list, or empty when the line is a comment or prose.
wxString OpcodeLine(wxString line)
{
	const int comment = line.Find(wxT("//"));
	if (comment != wxNOT_FOUND)
		line = line.Left(comment);
	line.Trim(true).Trim(false);
	if (line.StartsWith(wxT("OPER_")) || line.StartsWith(wxT("#define TYPE_DELTA")))
		return line;
	return wxString();
}

std::uint64_t Fingerprint(const wxString& text)
{
	const wxScopedCharBuffer utf8 = text.utf8_str();
	std::uint64_t h = 1469598103934665603ULL;
	for (size_t i = 0; i < utf8.length(); ++i) {
		h ^= (std::uint64_t)(unsigned char)utf8.data()[i];
		h *= 1099511628211ULL;
	}
	return h;
}

std::uint64_t OpcodeFingerprint(wxString* pathOut)
{
	const wxString path = CodeDefPath();
	if (pathOut)
		*pathOut = path;

	wxTextFile file;
	if (!file.Open(path, wxConvUTF8))
		return 0;

	wxString text;
	for (size_t i = 0; i < file.GetLineCount(); ++i) {
		const wxString line = OpcodeLine(file.GetLine(i));
		if (!line.empty())
			text << line << wxT("\n");
	}
	if (text.empty())
		return 0;
	return Fingerprint(text);
}

} // namespace

TEST(ByteCodeFormat, AnOpcodeChangeBumpsTheVersion) {
	wxString path;
	const std::uint64_t now = OpcodeFingerprint(&path);
	ASSERT_NE(now, 0ULL) << "could not read the opcode list from " << path.ToStdString();

	EXPECT_EQ(kAOTFormatVersion, kVersionOfThisFingerprint)
		<< "kAOTFormatVersion moved without the recorded opcode fingerprint. "
		   "Read codeDef.h, and if the opcode list changed, record the new "
		   "fingerprint below; if it did not, the version bump still belongs "
		   "in this pair so the two cannot drift.";

	EXPECT_EQ(now, kOpcodeFingerprintAtThatVersion)
		<< "codeDef.h's opcode list changed and kAOTFormatVersion is still "
		<< kAOTFormatVersion << ". Bump kAOTFormatVersion in byteCodeFormat.h "
		   "(the cache key is that number) and record the new fingerprint here. "
		   "The list was read from " << path.ToStdString();
}

// A version bump has to change the key. The old key was the build stamp, which
// does not. This pins the spelling: "<version>.<configuration digest>", then
// MD5, which is what fits in config_md5.
TEST(ByteCodeCacheKey, CarriesTheFormatVersion) {
	const wxString digest = wxT("0123456789abcdef0123456789abcdef");
	const wxString spelled = wxString::Format(wxT("%u.%s"),
		(unsigned)kAOTFormatVersion, digest);
	EXPECT_EQ(ibByteCodeCache::CacheKey(digest), ibMD5::ComputeMd5(spelled));

	const wxString next = wxString::Format(wxT("%u.%s"),
		(unsigned)kAOTFormatVersion + 1u, digest);
	EXPECT_NE(ibMD5::ComputeMd5(spelled), ibMD5::ComputeMd5(next))
		<< "two format versions produced one cache key";
}
