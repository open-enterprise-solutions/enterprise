#ifndef __ADVPROP_PICTURE_H__
#define __ADVPROP_PICTURE_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>

#include <map>
#include <optional>   // the configuration's pictures, as the row brings them

#include "frmclient/backend/backend_core.h"
#include "core/description/pictureDescription.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — the row's Pictures

// -----------------------------------------------------------------------
// ibPGExternalImageProperty
// -----------------------------------------------------------------------

// THE PICTURE CELLS:a picture of a file, read
// whole into the value (ibPGExternalImageProperty), and a picture by where it comes from — the engine's, the
// configuration's, a file's (wxPGPictureProperty).
class ibPGExternalImageProperty : public wxEditorDialogProperty {

public:

	ibPGExternalImageProperty(const wxString& label = wxPG_LABEL, const wxString& name = wxPG_LABEL, const wxVariant& value = wxVariant());

	virtual wxSize OnMeasureImage(int item) const override { return wxSize(16, 16); }
	virtual void OnCustomPaint(wxDC& dc,
		const wxRect& rect, wxPGPaintData& paintdata) override;

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override
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
		const wxVariant& value = wxVariant(),
		const std::optional<ibDataNode>& pictures = std::nullopt);

	virtual ~wxPGPictureProperty() override {}

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
