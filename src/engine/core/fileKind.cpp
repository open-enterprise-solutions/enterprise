#include "core/fileKind.h"
#include "core/fileSystem.h"   // ibPathOf — std::filesystem reads the extension

namespace {

// ONE ROW PER KIND, and the row is the whole truth about it. A new kind is a
// row here and nothing else — no second list to keep in step.
struct ibFileKindRow {
	ibFileKind    kind;
	const ibChar* extension;
	// The SOURCE string, marked for extraction and left untranslated here: this
	// table is static data, built before a locale is loaded. Translating at the
	// point of use is what lets the same table answer in whatever language the
	// session is running.
	const char*   description;
};

const ibFileKindRow s_kinds[] = {
	{ ibFileKind::Application, ibT("oap"), ibTRANSLATE("Application files") },
	{ ibFileKind::Tool,        ibT("otl"), ibTRANSLATE("Tool files")        },
	{ ibFileKind::Report,      ibT("orp"), ibTRANSLATE("Report files")      },
	{ ibFileKind::Table,       ibT("oxl"), ibTRANSLATE("Table files")       },
	{ ibFileKind::Log,         ibT("olg"), ibTRANSLATE("Log files")         },
	{ ibFileKind::Save,        ibT("osv"), ibTRANSLATE("Data files")        },
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

ibString ibFileExtension(ibFileKind kind)
{
	const ibFileKindRow* row = RowFor(kind);
	return row != nullptr ? ibString(row->extension) : ibString();
}

ibString ibFileMask(ibFileKind kind)
{
	const ibFileKindRow* row = RowFor(kind);
	return row != nullptr ? ibString::Format(ibT("*.%s"), row->extension) : ibString();
}

ibString ibFileFilter(ibFileKind kind)
{
	const ibFileKindRow* row = RowFor(kind);
	if (row == nullptr)
		return ibString();

	const ibString mask = ibFileMask(kind);
	const ibString description = ibGetTranslation(ibString::FromUTF8(row->description));
	return ibString::Format(ibT("%s (%s)|%s"), description, mask, mask);
}

bool ibFileKindOf(const ibString& path, ibFileKind& kind)
{
	// The EXTENSION, not the tail of the name: "report.orp.bak" is a backup of a
	// report, not a report.
	const ibString ext = ibString(ibPathOf(path).extension().wstring()).Mid(1).Lower();
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
