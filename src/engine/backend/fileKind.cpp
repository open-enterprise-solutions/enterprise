#include "backend/fileKind.h"

#include <wx/filename.h>
#include <wx/intl.h>

namespace {

// ONE ROW PER KIND, and the row is the whole truth about it. A new kind is a
// row here and nothing else — no second list to keep in step.
struct ibFileKindRow {
	ibFileKind    kind;
	const wxChar* extension;
	// The SOURCE string, marked for extraction and left untranslated here: this
	// table is static data, built before a locale is loaded. Translating at the
	// point of use is what lets the same table answer in whatever language the
	// session is running.
	const char*   description;
};

const ibFileKindRow s_kinds[] = {
	{ ibFileKind::Application, wxT("oap"), wxTRANSLATE("Application files") },
	{ ibFileKind::Tool,        wxT("otl"), wxTRANSLATE("Tool files")        },
	{ ibFileKind::Report,      wxT("orp"), wxTRANSLATE("Report files")      },
	{ ibFileKind::Table,       wxT("oxl"), wxTRANSLATE("Table files")       },
	{ ibFileKind::Log,         wxT("olg"), wxTRANSLATE("Log files")         },
	{ ibFileKind::Save,        wxT("osv"), wxTRANSLATE("Data files")        },
};

const ibFileKindRow* RowFor(ibFileKind kind)
{
	for (const ibFileKindRow& row : s_kinds) {
		if (row.kind == kind)
			return &row;
	}
	return nullptr;   // unreachable while the enum and the table agree
}

} // namespace

wxString ibFileExtension(ibFileKind kind)
{
	const ibFileKindRow* row = RowFor(kind);
	return row != nullptr ? wxString(row->extension) : wxString();
}

wxString ibFileMask(ibFileKind kind)
{
	const ibFileKindRow* row = RowFor(kind);
	return row != nullptr ? wxString::Format(wxT("*.%s"), row->extension) : wxString();
}

wxString ibFileFilter(ibFileKind kind)
{
	const ibFileKindRow* row = RowFor(kind);
	if (row == nullptr)
		return wxString();

	const wxString mask = ibFileMask(kind);
	const wxString description = wxGetTranslation(wxString::FromUTF8(row->description));
	return wxString::Format(wxT("%s (%s)|%s"), description, mask, mask);
}

bool ibFileKindOf(const wxString& path, ibFileKind& kind)
{
	// The EXTENSION, not the tail of the name: "report.orp.bak" is a backup of a
	// report, not a report.
	const wxString ext = wxFileName(path).GetExt().Lower();
	if (ext.IsEmpty())
		return false;

	for (const ibFileKindRow& row : s_kinds) {
		if (ext == row.extension) {
			kind = row.kind;
			return true;
		}
	}
	return false;
}
