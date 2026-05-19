/////////////////////////////////////////////////////////////////////////////
// ibIOWriter — abstract interface for text-format configuration writers.
//
// Counterpart of ibIOReader. Serialises live metadata / form / table-document
// objects to a documented schema. Path overload owns open/close; stream
// overload writes into caller-managed sinks.
//
// See docs/adr/0001-io-readerwriter-interfaces.md.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_IO_WRITER_H_
#define _IB_IO_WRITER_H_

#include "backend/backend.h"  // BACKEND_API

#include <wx/string.h>

class wxOutputStream;

class ibValueMetaObject;
class ibValueMetaObjectFormBase;
class ibSpreadsheetDescription;

class BACKEND_API ibIOWriter {
public:
	virtual ~ibIOWriter() = default;

	// MetaObject serialisation. Writes the structural fields (id, name,
	// type qualifiers, attributes, tabular sections, modules, predefined
	// values, MetaDescription bindings) into the implementation's
	// schema. Forms and table documents owned by `obj` are NOT walked
	// inline — callers serialise those via SaveForm / SaveTableDoc to
	// keep per-artifact files git-mergeable.
	virtual bool SaveMetaObject(const ibValueMetaObject& obj,
	                            const wxString& path) = 0;
	virtual bool SaveMetaObject(const ibValueMetaObject& obj,
	                            wxOutputStream& stream) = 0;

	// Form serialisation. Walks the control tree, captures per-control
	// properties and event bindings, embeds the module source verbatim.
	// The form data blob is NOT round-tripped via base64 — controls are
	// described structurally so git diffs reflect real changes.
	virtual bool SaveForm(const ibValueMetaObjectFormBase& form,
	                      const wxString& path) = 0;
	virtual bool SaveForm(const ibValueMetaObjectFormBase& form,
	                      wxOutputStream& stream) = 0;

	// Table document serialisation. Writes cells, row/col sizes,
	// breaks, named areas, groups, and per-cell styling.
	virtual bool SaveTableDoc(const ibSpreadsheetDescription& doc,
	                          const wxString& path) = 0;
	virtual bool SaveTableDoc(const ibSpreadsheetDescription& doc,
	                          wxOutputStream& stream) = 0;

	// Identifier of the format this writer emits. Embedded in the root
	// element of every artifact and checked by the matching reader.
	virtual wxString FormatId() const = 0;

	// Last error description (empty after a successful save).
	virtual wxString LastError() const = 0;
};

#endif // _IB_IO_WRITER_H_
