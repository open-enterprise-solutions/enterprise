#ifndef __ADVPROP_CHART_BINDING_H__
#define __ADVPROP_CHART_BINDING_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "frmclient/backend/backend_type.h"
#include "frmclient/backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the row's own answer

#include <vector>

// -----------------------------------------------------------------------
// ibPGChartBindingProperty
// -----------------------------------------------------------------------

// ⭐ ONE EDITOR FOR THE PROPERTIES THAT BIND A CHART BY ID — the old desktop's: the
// chart of an accounting, characteristic or calculation binding, the kind of accounting, the functional options. A
// catalog's owners, a document's registers and what it is entered on are the desktop's own cells, as there
// (advpropOwner.cpp, advpropRecord.cpp, advpropGeneration.cpp).
//
// ⭐ THE CHOICES ARRIVE WITH THE ROW (the server's GetValueList: Choices, Multiple when a set may be ticked), and the
// value is the property's node form — an Array of metaobject ids (ibMetaDescriptionMemory). Nothing is asked to draw or
// edit it: the dialog ticks, OK hands the ids back, and the row's Set sends them.
class ibPGChartBindingProperty : public ibPGProperty {
public:

	bool IsSet() const { return m_choiceMode == ibPropertyChoiceMode::Mult; }

	ibPGChartBindingProperty(const wxString& label = ibPG_LABEL, const wxString& name = ibPG_LABEL,
		const ibVariant& value = ibVariant(),
		const ibPropertyChoiceList& choices = ibPropertyChoiceList(),
		ibPropertyChoiceMode mode = ibPropertyChoiceMode::Single);

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool IntToValue(ibVariant& value,
		int number,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual ibPGEditorDialogAdapter* GetEditorDialog() const override;

	// The ids a value holds, and the same property's value holding others.
	static std::vector<ibMetaID> IdsOf(const ibVariant& value);
	static ibVariant ValueOf(const ibVariant& value, const std::vector<ibMetaID>& ids);

protected:
	ibPropertyChoiceMode m_choiceMode = ibPropertyChoiceMode::Single;
private:
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGChartBindingProperty);
};

#endif
