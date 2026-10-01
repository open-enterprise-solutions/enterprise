#ifndef _FILE_KIND_H__
#define _FILE_KIND_H__

////////////////////////////////////////////////////////////////////////////
//	Description : the file kinds this platform writes — one table
////////////////////////////////////////////////////////////////////////////
//
// WHY THIS EXISTS. Extensions used to be string literals at the point of use:
// "*.mcf" in the designer's template list, "*.edp" again in the enterprise
// one, "Data processor files (*.edp)|*.edp" spelled a third time in the
// metadata tree. Six kinds across eight files, and every one of them a place to
// forget when a name changes — which is exactly what a rename has to touch.
//
// So a kind answers for itself: what it is called and how a file dialog should
// describe it.
//
// NAMING. The names are OURS, not borrowed: `o` marks the family, the two
// letters that follow come from the word WE use for the thing.
//
//   oap   application     the whole application: metadata, modules, forms, rights
//   otl   tool            an external tool (what used to be a "data processor")
//   orp   report          an external report
//   oxl   table           a spreadsheet document
//   olg   log             the platform's own journal
//   osv   save            a data dump — the base written out, restorable elsewhere
//
// "Application", not "schema": what travels in that file includes MODULE CODE,
// and no reading of the word schema covers code. `schema` also already means
// something precise in this tree — the shape of the database tables — in a few
// hundred places.
//
// NO LEGACY NAMES. The old extensions (.mcf .edp .erp .obk) are not read: the
// platform is pre-release, so there is no installed base to carry, and carrying
// one would mean every dialog offering two names for one thing forever. A file
// made before the rename is opened by renaming it.
//
// The signature lives INSIDE the file (see serialization-io.md): an extension
// is a hint for the operating system and the person browsing a folder, never
// the proof of what a file is. This table is where that pairing will be stated
// once the signatures land.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_core.h"

#include <wx/string.h>

enum class ibFileKind {
	Application,   // .oap
	Tool,          // .otl
	Report,        // .orp
	Table,         // .oxl
	Log,           // .olg
	Save,          // .osv
};

// The extension, without the dot ("oap").
BACKEND_API wxString ibFileExtension(ibFileKind kind);

// The mask a template matches a path against: "*.oap".
BACKEND_API wxString ibFileMask(ibFileKind kind);

// A file-dialog filter for one kind, already worded:
//   "Application files (*.oap)|*.oap"
// The description is translated through the catalogs, so the dialog speaks the
// user's language while the mask stays what it is.
BACKEND_API wxString ibFileFilter(ibFileKind kind);

// Which kind a path belongs to, by extension. Answers false when nothing
// matches, rather than guessing a kind.
BACKEND_API bool ibFileKindOf(const wxString& path, ibFileKind& kind);

#endif // !_FILE_KIND_H__
