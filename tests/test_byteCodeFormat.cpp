// =============================================================================
// OES Enterprise — the bytecode format version is the cache's engine half
//
// The cache key used to be GetBuildStamp(), which is __DATE__/__TIME__ of
// core/build.cpp. core is a separate library and is not rebuilt when the
// compiler or the interpreter changes, so a stale blob kept its key.
//
// The key is kAOTFormatVersion, the hash compiler/engineFingerprint.cmake
// writes at build time from compiler/** and system/**, and the configuration
// digest. The opcode fingerprint stays as a second guard: it fails when
// codeDef.h moves and the version does not. A separate test shows that a
// changed built-in source is a different hash, and therefore a different key.
// =============================================================================

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "backend/compiler/byteCodeFormat.h"
#include "backend/compiler/cache/byteCodeCache.h"
#include "backend/utils/md5.hpp"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/textfile.h>
#include <wx/utils.h>

namespace {

// The opcode list as it was when kAOTFormatVersion was last set. Computed
// by OpcodeFingerprint(), which reads codeDef.h: every OPER_ enumerator and
// the TYPE_DELTA macros, comments stripped. 36: OPER_ITER is gone.
constexpr std::uint16_t kVersionOfThisFingerprint = 36;
constexpr std::uint64_t kOpcodeFingerprintAtThatVersion = 9806823567777943829ULL;

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

wxString RepoRoot()
{
	wxFileName here(wxString::FromUTF8(__FILE__));
	here.RemoveLastDir();   // tests/ -> the repository root
	here.SetFullName(wxEmptyString);
	here.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
	return here.GetPath();
}

// The hash the build script writes for these two directories. `substituteRel`
// empty hashes the tree as it is; otherwise that path, relative to backend/,
// is read from `substituteFile` instead.
wxString ScriptFingerprint(const wxString& substituteRel, const wxString& substituteFile)
{
	const wxString root = RepoRoot();
	const wxString backend = root + wxFILE_SEP_PATH + wxT("src") + wxFILE_SEP_PATH
		+ wxT("engine") + wxFILE_SEP_PATH + wxT("backend");
	const wxString script = backend + wxFILE_SEP_PATH + wxT("compiler")
		+ wxFILE_SEP_PATH + wxT("engineFingerprint.cmake");
	const wxString out = wxFileName(wxStandardPaths::Get().GetTempDir(),
		substituteRel.empty() ? wxT("oes-engine-fp.h") : wxT("oes-engine-fp-sub.h")).GetFullPath();
	wxString cmd = wxString::Format(
		wxT("cmake -DOES_COMPILER_DIR=\"%s\" -DOES_SYSTEM_DIR=\"%s\" -DOES_BACKEND_DIR=\"%s\" -DOES_FINGERPRINT_OUT=\"%s\""),
		backend + wxFILE_SEP_PATH + wxT("compiler"),
		backend + wxFILE_SEP_PATH + wxT("system"),
		backend,
		out);
	if (!substituteRel.empty())
		cmd += wxString::Format(wxT(" -DOES_SUBSTITUTE_REL=\"%s\" -DOES_SUBSTITUTE_FILE=\"%s\""),
			substituteRel, substituteFile);
	cmd += wxT(" -P \"") + script + wxT("\"");

	const long rc = wxExecute(cmd, wxEXEC_SYNC);
	EXPECT_EQ(rc, 0) << cmd.ToStdString();

	wxTextFile file;
	EXPECT_TRUE(file.Open(out, wxConvUTF8)) << out.ToStdString();
	wxString hash;
	const wxString marker = wxT("kEngineFingerprint[] = \"");
	for (size_t i = 0; i < file.GetLineCount(); ++i) {
		const wxString line = file.GetLine(i);
		const int at = line.Find(marker);
		if (at == wxNOT_FOUND)
			continue;
		const wxString rest = line.Mid(at + marker.length());
		const int end = rest.Find(wxUniChar('"'));
		if (end == wxNOT_FOUND)
			continue;
		hash = rest.Left(end);
	}
	wxRemoveFile(out);
	return hash;
}

// The spelling is "<version>.<engine hash>.<configuration digest>", then MD5,
// which is what fits in config_md5. The old key was the build stamp, which
// does not move when the compiler does.
TEST(ByteCodeCacheKey, CarriesTheFormatVersionAndTheEngineHash) {
	const wxString digest = wxT("0123456789abcdef0123456789abcdef");
	const wxString fingerprint = ibByteCodeCache::EngineFingerprint();
	ASSERT_EQ(fingerprint.length(), 64u);

	const wxString spelled = wxString::Format(wxT("%u.%s.%s"),
		(unsigned)kAOTFormatVersion, fingerprint, digest);
	EXPECT_EQ(ibByteCodeCache::CacheKey(digest), ibMD5::ComputeMd5(spelled));

	const wxString otherVersion = wxString::Format(wxT("%u.%s.%s"),
		(unsigned)kAOTFormatVersion + 1u, fingerprint, digest);
	EXPECT_NE(ibMD5::ComputeMd5(spelled), ibMD5::ComputeMd5(otherVersion));

	const wxString otherEngine = wxString::Format(wxT("%u.%s.%s"),
		(unsigned)kAOTFormatVersion, fingerprint + wxT("x"), digest);
	EXPECT_NE(ibMD5::ComputeMd5(spelled), ibMD5::ComputeMd5(otherEngine))
		<< "two engine hashes produced one cache key";
}

// A built-in lives under system/. Changing one is a different hash from the
// script the build runs, and that hash is what the key is made of.
TEST(ByteCodeCacheKey, ABuiltInChangeAltersTheKey) {
	const wxString real = ScriptFingerprint(wxString(), wxString());
	ASSERT_FALSE(real.empty());
	EXPECT_EQ(real, ibByteCodeCache::EngineFingerprint())
		<< "the header the backend was built with is not the script's hash of this tree";

	const wxString source = RepoRoot() + wxFILE_SEP_PATH + wxT("src") + wxFILE_SEP_PATH
		+ wxT("engine") + wxFILE_SEP_PATH + wxT("backend") + wxFILE_SEP_PATH
		+ wxT("system") + wxFILE_SEP_PATH + wxT("systemManager.cpp");
	const wxString changed = wxFileName(wxStandardPaths::Get().GetTempDir(),
		wxT("oes-systemManager-changed.cpp")).GetFullPath();
	{
		wxTextFile in;
		ASSERT_TRUE(in.Open(source, wxConvUTF8)) << source.ToStdString();
		wxTextFile out;
		ASSERT_TRUE(out.Create(changed));
		for (size_t i = 0; i < in.GetLineCount(); ++i)
			out.AddLine(in.GetLine(i));
		out.AddLine(wxT("/* a built-in that was not here */"));
		ASSERT_TRUE(out.Write());
	}

	const wxString altered = ScriptFingerprint(wxT("system/systemManager.cpp"), changed);
	wxRemoveFile(changed);
	ASSERT_FALSE(altered.empty());
	EXPECT_NE(altered, real);

	const wxString digest = wxT("0123456789abcdef0123456789abcdef");
	const wxString was = wxString::Format(wxT("%u.%s.%s"),
		(unsigned)kAOTFormatVersion, real, digest);
	const wxString now = wxString::Format(wxT("%u.%s.%s"),
		(unsigned)kAOTFormatVersion, altered, digest);
	EXPECT_NE(ibMD5::ComputeMd5(was), ibMD5::ComputeMd5(now));
	EXPECT_EQ(ibByteCodeCache::CacheKey(digest), ibMD5::ComputeMd5(was));
}
