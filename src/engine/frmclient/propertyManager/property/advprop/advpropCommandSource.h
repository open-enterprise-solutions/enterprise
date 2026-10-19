#ifndef __ADVPROP_COMMAND_SOURCE_H__
#define __ADVPROP_COMMAND_SOURCE_H__

#include <wx/propgrid/propgrid.h>

#include "frmclient/backend/backend_core.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — the command tree the row brings

class FRMCLIENT_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGCommandSourceProperty — the inspector picker for a button's COMMAND SOURCE
// -----------------------------------------------------------------------
//
// The desktop's (frontend/propertyManager/property/advprop/advpropCommandSource.h), the command-door twin of
// ibPGDataSourceProperty. A text-and-button cell whose "…" opens a DIALOG showing the form's command sources (own
// object · main attribute · each table · general) grouped as a tree; picking one binds its command-hop path to the
// button.
//
// The command sources are gathered by the form's command door (ibFrontendCommandReceiver, GatherFormCommands) over the
// form's commands and its attributes' — the server's: the row brings them (`CommandTree`, the form editor's command
// tree's rows), and the dialog lists those. A row without them (an older server) says so instead. "New command..." is
// the form editor's command tree's (its Create), not the picker's.
class ibPGCommandSourceProperty : public wxPGProperty {
public:

	const ibPropertyObject* GetPropertyObject() const { return m_propertyObject; }

	ibPGCommandSourceProperty(const ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant, const ibDataNode* tree = nullptr);

	// The row's command tree — the three sections, sub-groups and commands; none: null.
	const ibDataNode* GetCommandTree() const { return m_hasTree ? &m_tree : nullptr; }

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	// The bound command's OWN picture in the cell, before the text — on the desktop resolved live through the
	// command door when the value is set; none on the client yet.
	virtual void OnSetValue() override;

	virtual bool StringToValue(wxVariant& variant, const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

private:
	const ibPropertyObject* m_propertyObject = nullptr;   // the command bar item — vends the form to gather from
	ibDataNode m_tree;                                     // the row's `CommandTree`, as the server wrote it
	bool m_hasTree = false;
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGCommandSourceProperty)
};

#endif // !__ADVPROP_COMMAND_SOURCE_H__
