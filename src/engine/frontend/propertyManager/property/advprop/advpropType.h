#ifndef __ADVPROP_TYPE_H__
#define __ADVPROP_TYPE_H__

#include <wx/propgrid/propgrid.h>
#include "backend/backend_type.h"

class BACKEND_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGTypeProperty
// -----------------------------------------------------------------------

class ibPGTypeProperty : public wxPGProperty {
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGTypeProperty);
private:
	wxPGChoices GetDateTime();
	void FillByClsid(const ibSelectorDataType &selectorDataType, const ibClassID& clsid);
public:

	const ibPropertyObject* GetPropertyObject() const { return m_ownerProperty; }

	ibPGTypeProperty(const ibPropertyObject* property = nullptr, const ibSelectorDataType& selectorDataType = ibSelectorDataType::ibSelectorDataType_any, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant);

	virtual wxString ValueToString(wxVariant& value, wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override { return value.GetString(); }
	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override { return false; }

	virtual bool IntToValue(wxVariant& value,
		int number,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual wxVariant ChildChanged(wxVariant& thisValue,
		int childIndex,
		wxVariant& childValue) const override;

	virtual void RefreshChildren() override;

	// GetChoiceSelection needs to overridden since m_choices is
	// used and value is integer, but it is not index.
	virtual int GetChoiceSelection() const override { return wxNOT_FOUND; }

	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

	// The value's picture, read off the value when the grid draws (advpropValuePicture.h).
	virtual wxSize OnMeasureImage(int item = -1) const override;
	virtual void OnCustomPaint(wxDC& dc, const wxRect& rect, wxPGPaintData& paintdata) override;

protected:

	// The chosen type's own picture (a metaobject's, a family's metatype's, AnyRef's); none for several.
	wxBitmapBundle GetValuePicture() const;

	const ibPropertyObject* m_ownerProperty = nullptr;

	wxUIntProperty* m_precision;
	wxUIntProperty* m_scale;
	wxEnumProperty* m_date_time;
	wxUIntProperty* m_length;

	std::map<int, ibClassID> m_valChoices;
};

#endif