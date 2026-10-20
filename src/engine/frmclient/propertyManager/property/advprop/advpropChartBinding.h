#ifndef __ADVPROP_CHART_BINDING_H__
#define __ADVPROP_CHART_BINDING_H__

#include <wx/propgrid/propgrid.h>
#include "frmclient/backend/backend_type.h"
#include "frmclient/backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the row's own answer

#include <vector>

// -----------------------------------------------------------------------
// ibPGChartBindingProperty
// -----------------------------------------------------------------------

// ⭐ ONE EDITOR FOR THE PROPERTIES THAT BIND A CHART BY ID — the desktop's (frontend advpropChartBinding.h): the
// chart of an accounting, characteristic or calculation binding, the kind of accounting, the functional options. A
// catalog's owners, a document's registers and what it is entered on are the desktop's own cells, as there
// (advpropOwner.cpp, advpropRecord.cpp, advpropGeneration.cpp).
//
// ⭐ THE CHOICES ARRIVE WITH THE ROW (the server's GetValueList: Choices, Multiple when a set may be ticked), and the
// value is the property's node form — an Array of metaobject ids (ibMetaDescriptionMemory). Nothing is asked to draw or
// edit it: the dialog ticks, OK hands the ids back, and the row's Set sends them.
class ibPGChartBindingProperty : public wxPGProperty {
public:

	bool IsSet() const { return m_choiceMode == ibPropertyChoiceMode::Mult; }

	ibPGChartBindingProperty(const wxString& label = wxPG_LABEL, const wxString& name = wxPG_LABEL,
		const wxVariant& value = wxVariant(),
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

	// The ids a value holds, and the same property's value holding others.
	static std::vector<ibMetaID> IdsOf(const wxVariant& value);
	static wxVariant ValueOf(const wxVariant& value, const std::vector<ibMetaID>& ids);

protected:
	ibPropertyChoiceMode m_choiceMode = ibPropertyChoiceMode::Single;
private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGChartBindingProperty);
};

#endif
