#ifndef __ADVPROP_SOURCE_H__
#define __ADVPROP_SOURCE_H__

#include <wx/propgrid/propgrid.h>

#include "frmclient/backend/backend_core.h"
#include "core/description/sourceDescription.h"

#include <functional>

class FRMCLIENT_API ibPropertyObject;

// THE SOURCE WINDOW, OPENED BY WHOEVER HOLDS THE FORM — the tree to bind to is the form's attribute tree, and the
// thin designer's form editor holds it (and asks the server for what lies under a reference). It sets the picker once;
// the cell calls it with the control the binding is of, what it may be bound to (the row's SourceKind: 0 a table,
// 1 a column of its table, 2 a field), the path of the table a column's box is bound to, and the binding — changed
// in place on OK. False: cancelled, or no form holds the control.
typedef std::function<bool(wxWindow* parent, const ibPropertyObject* owner, int kind,
	const ibSourceDescription& table, ibSourceDescription& binding)> ibSourcePicker;
FRMCLIENT_API void ibSetSourcePicker(const ibSourcePicker& picker);

// -----------------------------------------------------------------------
// ibPGDataSourceProperty
// -----------------------------------------------------------------------

// THE DESKTOP'S SOURCE CELL (frontend/propertyManager/property/advprop/advpropSource.h) — a control's binding to a value
// of its form's data: the path as text, the "…" opening the tree of the form's attributes and their columns.
//
// The tree is the form's attribute tree (ibSourcePicker), filtered as the row says (SourceKind, Parent); the Type row
// the desktop unfolds beneath — the type the source's leaf has — is the server's and is not shown. The cell reads as
// the server wrote it and an empty text unbinds.
class ibPGDataSourceProperty : public wxPGProperty {
public:

	ibPGDataSourceProperty(const ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const wxVariant& value = wxNullVariant, int kind = 2, const ibSourceDescription& table = ibSourceDescription());

	const ibPropertyObject* GetOwner() const { return m_owner; }
	int GetKind() const { return m_kind; }
	const ibSourceDescription& GetTable() const { return m_table; }

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

private:
	const ibPropertyObject* m_owner = nullptr;   // the control the binding is of
	int m_kind = 2;                              // what it may be bound to (the row's SourceKind; none: a field)
	ibSourceDescription m_table;                 // a column's: the path of the table its box is bound to
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGDataSourceProperty)
};

#endif
