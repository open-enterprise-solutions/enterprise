#ifndef __ADVPROP_TYPE_H__
#define __ADVPROP_TYPE_H__

#include <map>
#include <vector>

#include "deskgui/univ/propgrid/propgrid.h"
#include "core/clsid.h"

// -----------------------------------------------------------------------
// ibPGTypeProperty
// -----------------------------------------------------------------------

// THE TYPE EDITOR:the list of types, the qualifiers
// under it, the picker behind its button. What it lists is the property's — the types the server offered it (the
// row's, ibPropertySerializable::GetValueList) — where the desktop's asked the configuration for them (FillByClsid).
class ibPGTypeProperty : public ibPGProperty {
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGTypeProperty);
private:
	ibPGChoices GetDateTime();
public:

	const std::vector<ibClassID>& GetTypeList() const { return m_listType; }
	bool IsMultiple() const { return m_multiple; }

	ibPGTypeProperty(const std::vector<ibClassID>& listType = std::vector<ibClassID>(), bool multiple = true,
		const wxString& label = ibPG_LABEL, const wxString& name = ibPG_LABEL, const ibVariant& value = ibVariant());

	virtual wxString ValueToString(ibVariant& value, ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override { return value.GetString(); }
	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override { return false; }

	virtual bool IntToValue(ibVariant& value,
		int number,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual ibVariant ChildChanged(ibVariant& thisValue,
		int childIndex,
		ibVariant& childValue) const override;

	virtual void RefreshChildren() override;

	// GetChoiceSelection needs to overridden since m_choices is
	// used and value is integer, but it is not index.
	virtual int GetChoiceSelection() const override { return wxNOT_FOUND; }

	virtual ibPGEditorDialogAdapter* GetEditorDialog() const override;

	// The value's picture, read off the value when the grid draws (advpropValuePicture.h).
	virtual wxSize OnMeasureImage(int item = -1) const override;
	virtual void OnCustomPaint(wxDC& dc, const wxRect& rect, ibPGPaintData& paintdata) override;

protected:

	// The chosen type's own picture (a metaobject's, a family's metatype's, AnyRef's); none for several.
	wxBitmapBundle GetValuePicture() const;

	std::vector<ibClassID> m_listType;
	bool m_multiple = true;

	ibUIntProperty* m_precision;
	ibUIntProperty* m_scale;
	ibEnumProperty* m_date_time;
	ibUIntProperty* m_length;

	std::map<int, ibClassID> m_valChoices;
};

#endif
