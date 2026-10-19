#ifndef __ADVPROP_CHART_BINDING_H__
#define __ADVPROP_CHART_BINDING_H__

#include <wx/propgrid/propgrid.h>
#include "frmclient/backend/backend_type.h"
#include "frmclient/backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the row's own answer

#include <vector>

// -----------------------------------------------------------------------
// ibPGChartBindingProperty
// -----------------------------------------------------------------------

// ⭐ ONE EDITOR FOR EVERY PROPERTY THAT NAMES METAOBJECTS BY ID — the desktop's (frontend advpropChartBinding.h): the
// chart of an accounting, characteristic or calculation binding, the kind of accounting, the functional options, and
// here also a catalog's owners, a document's registers and what it is entered on (the desktop's own Owner, Record and
// Generation cells are copies of this one, down to the dialog).
//
// ⭐ THE CHOICES ARRIVE WITH THE ROW (the server's GetValueList: Choices, Multiple when a set may be ticked), and the
// value is the property's node form — an Array of metaobject ids (ibMetaDescriptionMemory). Nothing is asked to draw or
// edit it: the dialog ticks, OK hands the ids back, and the row's Set sends them.
class ibPGChartBindingProperty : public wxPGProperty {
public:

	bool IsSet() const { return m_choiceMode == ibPropertyChoiceMode::Mult; }

	ibPGChartBindingProperty(const wxString& label = wxPG_LABEL, const wxString& name = wxPG_LABEL,
		const std::vector<ibMetaID>& value = std::vector<ibMetaID>(),
		const ibPropertyChoiceList& choices = ibPropertyChoiceList(),
		ibPropertyChoiceMode mode = ibPropertyChoiceMode::Single);

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool IntToValue(wxVariant& value,
		int number,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

	// The ids a value holds, and a value holding them.
	static std::vector<ibMetaID> IdsOf(const wxVariant& value);
	static wxVariant ValueOf(const std::vector<ibMetaID>& ids);

protected:
	ibPropertyChoiceMode m_choiceMode = ibPropertyChoiceMode::Single;
private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGChartBindingProperty);
};

#endif
