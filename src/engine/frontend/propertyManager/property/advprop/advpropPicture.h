#ifndef __ADVPROP_PICTURE_H__
#define __ADVPROP_PICTURE_H__

#include <wx/propgrid/propgrid.h>

#include "backend/backend_core.h"
#include "core/description/pictureDescription.h"

// -----------------------------------------------------------------------
// ibPGExternalImageProperty
// -----------------------------------------------------------------------

class ibPGExternalImageProperty : public wxEditorDialogProperty {

public:

	ibPGExternalImageProperty(const wxString& label = wxPG_LABEL, const wxString& name = wxPG_LABEL, const wxVariant& value = wxVariant());

	virtual wxSize OnMeasureImage(int item) const override { return wxSize(16, 16); }
	virtual void OnCustomPaint(wxDC& dc,
		const wxRect& rect, wxPGPaintData& paintdata) override;

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const
	{
		return value.GetString();
	}

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool DisplayEditorDialog(wxPropertyGrid* pg, wxVariant& value) override;

protected:

	wxString m_basePath;
	wxString m_initialPath;
	wxString m_wildcard;

	int m_indFilter;

	wxBitmap   m_bitmap; // final thumbnail area

private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGExternalImageProperty);
};

// -----------------------------------------------------------------------
// wxPGPictureProperty
// -----------------------------------------------------------------------

class wxPGPictureProperty : public wxPGProperty {
public:

	wxPGPictureProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const wxVariant& value = wxVariant());

	virtual ~wxPGPictureProperty() {}

	virtual wxString ValueToString(wxVariant& value, wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override { return value.GetString(); }
	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override {
		return false;
	}

	virtual void RefreshChildren() override;
	virtual wxVariant ChildChanged(wxVariant& thisValue, int childIndex,
		wxVariant& childValue) const override;

protected:

	wxEnumProperty* m_propertySource;

	wxEditEnumProperty* m_propertyBackend;
	wxEditEnumProperty* m_propertyConfiguration;
	ibPGExternalImageProperty* m_propertyFile;

	std::map<int, ibClassID> m_valChoices;
	std::map<int, ibGuid> m_confChoices;

private:
	WX_PG_DECLARE_PROPERTY_CLASS(wxPGPictureProperty);
};

#endif