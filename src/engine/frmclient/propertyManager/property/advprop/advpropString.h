#ifndef __ADVPROP_STRING_H__
#define __ADVPROP_STRING_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/advprops.h"

#include "frmclient/backend/backend_core.h"
#include "frmclient/backend/backend_localization.h"

#include "frmclient/win/dlgs/translateConstructor/translateConstructor.h"   // the window both translated properties open

// -----------------------------------------------------------------------
// ibPGStringProperty
// -----------------------------------------------------------------------

// The grid's string cell over the property's value: what it shows and takes is the grid's text, read from the node and
// written back as one.
class ibPGStringProperty : public ibStringProperty {
public:

	ibPGStringProperty(const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL,
		const ibVariant& value = ibVariant());

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

private:

	IB_PG_DECLARE_PROPERTY_CLASS(ibPGStringProperty);
};

// -----------------------------------------------------------------------
// ibUStringProperty
// -----------------------------------------------------------------------

class ibUStringProperty : public ibPGStringProperty {
public:

	enum {
		string_default = 0,
		allow_empty,
	};

	ibUStringProperty(const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL,
		const ibVariant& value = ibVariant(), const int flags = string_default) :
		ibPGStringProperty(label, name, value), m_strFlags(flags)
	{
	}

	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

private:

	int m_strFlags;

	IB_PG_DECLARE_PROPERTY_CLASS(ibUStringProperty);
};

// -----------------------------------------------------------------------
// ibUEStringProperty
// -----------------------------------------------------------------------

class ibUEStringProperty : public ibUStringProperty {
public:

	ibUEStringProperty(const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL,
		const ibVariant& value = ibVariant(), const int flags = allow_empty) :
		ibUStringProperty(label, name, value, flags)
	{
	}

	IB_PG_DECLARE_PROPERTY_CLASS(ibUEStringProperty);
};

// -----------------------------------------------------------------------
// ibTStringProperty
// -----------------------------------------------------------------------

class ibTStringProperty : public ibLongStringProperty {

public:
	// ⭐ IT CARRIES THE TRANSLATIONS, NOT THE FORMAT. The grid holds the value the property holds — its node the raw
	// text, every language — and the editor reads and writes languages (ibTranslateString) instead of taking apart
	// `en = '…';` and putting it back together in three different places.
	ibTStringProperty(const class FRMCLIENT_API ibPropertyObject* property = nullptr, const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL,
		const ibVariant& value = ibVariant());

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

protected:
	virtual bool DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value) override;

	// What edits one language's box through a window of its own — nothing for a caption.
	virtual ibDialogTranslateConstructor::ibBoxEditor GetBoxEditor() const { return ibDialogTranslateConstructor::ibBoxEditor(); }
private:

	const class FRMCLIENT_API ibPropertyObject* m_ownerProperty = nullptr;

	IB_PG_DECLARE_PROPERTY_CLASS(ibTStringProperty);
};

// -----------------------------------------------------------------------
// ibFormatStringProperty
// -----------------------------------------------------------------------

// THE FORMAT — a translated string whose every language holds a format string. The same cell and the
// same window as a caption; what it adds is the `...` beside each language's box, which opens the
// format string constructor over that language's string.
class ibFormatStringProperty : public ibTStringProperty {
public:
	ibFormatStringProperty(const class FRMCLIENT_API ibPropertyObject* property = nullptr, const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL,
		const ibVariant& value = ibVariant()) :
		ibTStringProperty(property, label, name, value)
	{
	}

protected:
	virtual ibDialogTranslateConstructor::ibBoxEditor GetBoxEditor() const override;
private:
	IB_PG_DECLARE_PROPERTY_CLASS(ibFormatStringProperty);
};

// -----------------------------------------------------------------------
// ibMStringProperty
// -----------------------------------------------------------------------

class ibMStringProperty : public ibLongStringProperty {
public:
	ibMStringProperty(const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL,
		const ibVariant& value = ibVariant());

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

protected:

	virtual bool DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value) override;

private:

	IB_PG_DECLARE_PROPERTY_CLASS(ibMStringProperty);
};

#endif