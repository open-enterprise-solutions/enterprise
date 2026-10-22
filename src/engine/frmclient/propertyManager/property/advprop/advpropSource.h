#ifndef __ADVPROP_SOURCE_H__
#define __ADVPROP_SOURCE_H__

#include <wx/propgrid/propgrid.h>

#include "frmclient/backend/backend_core.h"
#include "frmclient/propertyManager/property/advprop/advpropType.h"   // the Type row beneath — the desktop's
#include "core/description/sourceDescription.h"
#include "protocol/protocol.h"   // ibProtocol::ibSchema::ibProperties — SourceDataType, ibSourceDataType

#include <functional>

class FRMCLIENT_API ibPropertyObject;

// THE SOURCE WINDOW, OPENED BY WHOEVER HOLDS THE FORM — the thin designer's form editor (it asks the server for the tree
// as the window opens, and for what lies under a reference). It sets the picker once; the cell calls it with the control
// the binding is of, the property's name (what the server's editor of it is asked by), what it may be bound to (the row's
// SourceDataType), and the binding — changed in place on OK. False: cancelled, or no form holds the control.
typedef std::function<bool(wxWindow* parent, const ibPropertyObject* owner, const wxString& name,
	ibProtocol::ibSchema::ibProperties::ibSourceDataType filter, ibSourceDescription& binding)> ibSourcePicker;
FRMCLIENT_API void ibSetSourcePicker(const ibSourcePicker& picker);

// -----------------------------------------------------------------------
// ibPGDataSourceProperty
// -----------------------------------------------------------------------

// THE DESKTOP'S SOURCE CELL (frontend/propertyManager/property/advprop/advpropSource.h) — a control's binding to a value
// of its form's data: the path as text, the "…" opening the tree of the form's attributes and their columns, and the
// type the binding holds in a Type row beneath, read only and shown while bound — one property, both halves in its value
// as the server wrote them (the path, and SourceType beside it).
//
// The tree is the server's (Sources — the desktop's window's, its labels), asked as the window opens, filtered as the
// row says (SourceDataType). The cell reads as the server wrote it and an empty text unbinds.
class ibPGDataSourceProperty : public wxPGProperty {
public:

	ibPGDataSourceProperty(const ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const wxVariant& value = wxNullVariant,
		ibProtocol::ibSchema::ibProperties::ibSourceDataType filter = ibProtocol::ibSchema::ibProperties::ibSourceDataType::Attribute,
		const std::vector<ibClassID>& types = std::vector<ibClassID>());

	const ibPropertyObject* GetOwner() const { return m_owner; }
	ibProtocol::ibSchema::ibProperties::ibSourceDataType GetFilterSourceDataType() const { return m_filterSourceDataType; }

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual void RefreshChildren() override;
	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

private:
	const ibPropertyObject* m_owner = nullptr;   // the control the binding is of
	// what it may be bound to (the row's SourceDataType; none: an attribute)
	ibProtocol::ibSchema::ibProperties::ibSourceDataType m_filterSourceDataType =
		ibProtocol::ibSchema::ibProperties::ibSourceDataType::Attribute;
	ibPGTypeProperty* m_typeSelector = nullptr;  // the Type row — the type the binding holds
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGDataSourceProperty)
};

#endif
