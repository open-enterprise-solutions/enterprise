#ifndef __ADVPROP_TYPE_H__
#define __ADVPROP_TYPE_H__

#include <map>
#include <vector>

#include <wx/propgrid/propgrid.h>
#include "core/clsid.h"

// -----------------------------------------------------------------------
// ibPGTypeProperty
// -----------------------------------------------------------------------

// THE TYPE EDITOR:the list of types, the qualifiers
// under it, the picker behind its button. What it lists is the property's — the types the server offered it (the
// row's, ibPropertySerializable::GetValueList) — where the desktop's asked the configuration for them (FillByClsid).
class ibPGTypeProperty : public wxPGProperty {
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGTypeProperty);
private:
	wxPGChoices GetDateTime();
public:

	const std::vector<ibClassID>& GetTypeList() const { return m_listType; }
	bool IsMultiple() const { return m_multiple; }

	ibPGTypeProperty(const std::vector<ibClassID>& listType = std::vector<ibClassID>(), bool multiple = true,
		const wxString& label = wxPG_LABEL, const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant);

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

	std::vector<ibClassID> m_listType;
	bool m_multiple = true;

	wxUIntProperty* m_precision;
	wxUIntProperty* m_scale;
	wxEnumProperty* m_date_time;
	wxUIntProperty* m_length;

	std::map<int, ibClassID> m_valChoices;
};

#endif
