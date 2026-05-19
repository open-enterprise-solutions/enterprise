/////////////////////////////////////////////////////////////////////////////
// ibIOReader — abstract interface for text-format configuration loaders.
//
// Concrete implementations (ibXmlReader, ibJsonReader) parse a documented
// schema and populate live metadata / form / table-document objects. The
// path overload owns the open/close lifecycle; the stream overload lets
// daemon endpoints feed in-memory buffers without touching the filesystem.
//
// See docs/adr/0001-io-readerwriter-interfaces.md for the architectural
// rationale, format identifiers, and version policy.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_IO_READER_H_
#define _IB_IO_READER_H_

#include "backend/backend.h"  // BACKEND_API

#include <wx/string.h>

class wxInputStream;

class ibValueMetaObject;
class ibValueMetaObjectFormBase;
class ibSpreadsheetDescription;

class BACKEND_API ibIOReader {
public:
	virtual ~ibIOReader() = default;

	// MetaObject (Catalog / Document / Register / etc.). Populates `obj`
	// in place. Returns false on parse failure, schema mismatch, or any
	// I/O error; call LastError() for a diagnostic.
	virtual bool LoadMetaObject(const wxString& path,
	                            ibValueMetaObject& obj) = 0;
	virtual bool LoadMetaObject(wxInputStream& stream,
	                            ibValueMetaObject& obj) = 0;

	// Form definition (control tree + module text). Operates on
	// ibValueMetaObjectFormBase so both ibValueMetaObjectForm and
	// ibValueMetaObjectCommonForm round-trip.
	virtual bool LoadForm(const wxString& path,
	                      ibValueMetaObjectFormBase& form) = 0;
	virtual bool LoadForm(wxInputStream& stream,
	                      ibValueMetaObjectFormBase& form) = 0;

	// Table document (MXL-equivalent spreadsheet template). Loads cell
	// data, area definitions, row/col groups, breaks, and per-cell
	// styling into ibSpreadsheetDescription.
	virtual bool LoadTableDoc(const wxString& path,
	                          ibSpreadsheetDescription& doc) = 0;
	virtual bool LoadTableDoc(wxInputStream& stream,
	                          ibSpreadsheetDescription& doc) = 0;

	// Identifier of the format this reader handles — e.g. "oes-xml-1.0",
	// "oes-json-1.0". Used by registries to dispatch on a "format=" hint
	// without pattern-matching paths.
	virtual wxString FormatId() const = 0;

	// Last error description (empty after a successful load). Stable
	// across the lifetime of the reader instance until the next Load*
	// call resets it.
	virtual wxString LastError() const = 0;
};

#endif // _IB_IO_READER_H_
