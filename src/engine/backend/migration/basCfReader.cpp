/////////////////////////////////////////////////////////////////////////////
// basCfReader — see header. Detection-only stub; the unpacker is deferred
// per the spec.
/////////////////////////////////////////////////////////////////////////////

#include "basCfReader.hpp"

#include <wx/filename.h>
#include <wx/file.h>

namespace migration {
namespace bas {
namespace {

// 1С / BAS .cf files start with a 16-byte signature that always begins
// with the bytes 0xFF 0xFF 0xFF 0x7F followed by a length encoded as
// little-endian uint32 + four zero bytes. We don't need to validate
// the exact contents — recognising the first four bytes is enough to
// distinguish from a stray file the caller passed in.
//
// (Community references: github.com/e8tools/v8unpack — same magic.)
bool LooksLikeCf(const unsigned char* head, size_t n)
{
	if (n < 4) return false;
	return head[0] == 0xFF && head[1] == 0xFF
	    && head[2] == 0xFF && head[3] == 0x7F;
}

} // namespace

CfReadResult ReadCfArchive(const wxString& path)
{
	CfReadResult out;

	if (path.empty()) {
		out.status    = CfStatus::FileMissing;
		out.errorCode = wxT("OES_E_BAS_INVALID_INPUT");
		out.message   = wxT("cfPath is required");
		return out;
	}
	if (!wxFileName::FileExists(path)) {
		out.status    = CfStatus::FileMissing;
		out.errorCode = wxT("OES_E_BAS_INVALID_INPUT");
		out.message   = wxString::Format(wxT(".cf file not found: %s"), path);
		return out;
	}

	wxFile f(path, wxFile::read);
	if (!f.IsOpened()) {
		out.status    = CfStatus::FileMissing;
		out.errorCode = wxT("OES_E_BAS_PARSE_FAIL");
		out.message   = wxString::Format(wxT("could not open .cf: %s"), path);
		return out;
	}

	unsigned char head[16] = {0};
	const ssize_t got = f.Read(head, sizeof(head));
	if (got < 4) {
		out.status    = CfStatus::NotACfFile;
		out.errorCode = wxT("OES_E_BAS_PARSE_FAIL");
		out.message   = wxString::Format(
			wxT("file too small to be a .cf archive: %s"), path);
		return out;
	}

	if (!LooksLikeCf(head, static_cast<size_t>(got))) {
		out.status    = CfStatus::NotACfFile;
		out.errorCode = wxT("OES_E_BAS_PARSE_FAIL");
		out.message   = wxT("not a recognised .cf archive (header magic mismatch)");
		return out;
	}

	// Recognised — but unpacker not implemented in v1.
	out.status    = CfStatus::Unsupported;
	out.errorCode = wxT("OES_E_BAS_CF_UNSUPPORTED");
	out.message   = wxT(
		"binary .cf unpacking is deferred. Workflow: open the archive in the "
		"1С / BAS Configurator and run \"Configuration > Dump configuration "
		"files\" to export XML, then call import_bas_xml on the resulting "
		"Configuration.xml.");
	return out;
}

} // namespace bas
} // namespace migration
