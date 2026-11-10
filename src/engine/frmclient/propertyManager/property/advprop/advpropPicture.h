#ifndef __ADVPROP_PICTURE_H__
#define __ADVPROP_PICTURE_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/advprops.h"

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
// configuration's, a file's (ibPGPictureProperty).
class ibPGExternalImageProperty : public ibEditorDialogProperty {

public:

	ibPGExternalImageProperty(const wxString& label = ibPG_LABEL, const wxString& name = ibPG_LABEL, const ibVariant& value = ibVariant());

	virtual wxSize OnMeasureImage(int item) const override { return wxSize(16, 16); }
	virtual void OnCustomPaint(wxDC& dc,
		const wxRect& rect, ibPGPaintData& paintdata) override;

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override
	{
		return value.GetString();
	}

	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value) override;

protected:

	wxString m_basePath;
	wxString m_initialPath;
	wxString m_wildcard;

	int m_indFilter;

	wxBitmap   m_bitmap; // final thumbnail area

private:
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGExternalImageProperty);
};

// -----------------------------------------------------------------------
// ibPGPictureProperty
// -----------------------------------------------------------------------

class ibPGPictureProperty : public ibPGProperty {
public:

	ibPGPictureProperty(const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL,
		const ibVariant& value = ibVariant(),
		const std::optional<ibDataNode>& pictures = std::nullopt,
		const std::optional<ibDataNode>& classes = std::nullopt);

	virtual ~ibPGPictureProperty() override {}

	virtual wxString ValueToString(ibVariant& value, ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override { return value.GetString(); }
	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override {
		return false;
	}

	virtual void RefreshChildren() override;
	virtual ibVariant ChildChanged(ibVariant& thisValue, int childIndex,
		ibVariant& childValue) const override;

protected:

	ibEnumProperty* m_propertySource;

	ibEditEnumProperty* m_propertyBackend;
	ibEditEnumProperty* m_propertyConfiguration;
	ibPGExternalImageProperty* m_propertyFile;

	std::map<int, ibClassID> m_valChoices;
	std::map<int, ibGuid> m_confChoices;

private:
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGPictureProperty);
};

#endif
