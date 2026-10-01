#ifndef __ADVPROP_STRING_H__
#define __ADVPROP_STRING_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>

#include "backend/backend_core.h"
#include "backend/backend_localization.h"

#include "frontend/win/dlgs/translateConstructor/translateConstructor.h"   // the window both translated properties open

// -----------------------------------------------------------------------
// ibUStringProperty
// -----------------------------------------------------------------------

class ibUStringProperty : public wxStringProperty {
public:

	enum {
		string_default = 0,
		allow_empty,
	};

	ibUStringProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const wxString& value = wxEmptyString, const int flags = string_default) :
		wxStringProperty(label, name, value), m_strFlags(flags)
	{
	}

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

private:

	int m_strFlags;

	WX_PG_DECLARE_PROPERTY_CLASS(ibUStringProperty);
};

// -----------------------------------------------------------------------
// ibUEStringProperty
// -----------------------------------------------------------------------

class ibUEStringProperty : public ibUStringProperty {
public:

	ibUEStringProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const wxString& value = wxEmptyString, const int flags = allow_empty) :
		ibUStringProperty(label, name, value, flags)
	{
	}

	WX_PG_DECLARE_PROPERTY_CLASS(ibUEStringProperty);
};

// -----------------------------------------------------------------------
// wxTStringProperty
// -----------------------------------------------------------------------

class wxTStringProperty : public wxLongStringProperty {

public:
	// ⭐ IT CARRIES THE TRANSLATIONS, NOT THE FORMAT. The grid holds the same cell the property
	// holds (ibVariantDataTranslate), so the editor reads and writes languages instead of taking
	// apart `en = '…';` and putting it back together in three different places.
	wxTStringProperty(const class BACKEND_API ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const ibTranslateString& value = ibTranslateString());

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

protected:
	virtual bool DisplayEditorDialog(wxPropertyGrid* pg, wxVariant& value) override;

	// What edits one language's box through a window of its own — nothing for a caption.
	virtual ibDialogTranslateConstructor::ibBoxEditor GetBoxEditor() const { return ibDialogTranslateConstructor::ibBoxEditor(); }
private:

	const class BACKEND_API ibPropertyObject* m_ownerProperty = nullptr;

	WX_PG_DECLARE_PROPERTY_CLASS(wxTStringProperty);
};

// -----------------------------------------------------------------------
// wxFormatStringProperty
// -----------------------------------------------------------------------

// THE FORMAT — a translated string whose every language holds a format string. The same cell and the
// same window as a caption; what it adds is the `...` beside each language's box, which opens the
// format string constructor over that language's string.
class wxFormatStringProperty : public wxTStringProperty {
public:
	wxFormatStringProperty(const class BACKEND_API ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const ibTranslateString& value = ibTranslateString()) :
		wxTStringProperty(property, label, name, value)
	{
	}

protected:
	virtual ibDialogTranslateConstructor::ibBoxEditor GetBoxEditor() const override;
private:
	WX_PG_DECLARE_PROPERTY_CLASS(wxFormatStringProperty);
};

// -----------------------------------------------------------------------
// wxMStringProperty
// -----------------------------------------------------------------------

class wxMStringProperty : public wxLongStringProperty {
public:
	wxMStringProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const wxString& value = wxEmptyString) :
		wxLongStringProperty(label, name, value)
	{
	}

protected:

	virtual bool DisplayEditorDialog(wxPropertyGrid* pg, wxVariant& value) override;

private:

	WX_PG_DECLARE_PROPERTY_CLASS(wxMStringProperty);
};

#endif