#include <wx/propgrid/advprops.h>
#include <wx/propgrid/propgrid.h>   // wxPropertyGrid::RegisterEditorClass
#include <wx/datectrl.h>            // wxDatePickerCtrl — the grid's picker, told the date

#include "frmclient/backend/backend_core.h"   // ibDateTime — the date as the server stores it
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the date's node
#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

namespace {

// The date a value holds, as the server stores it (a Date node); the empty date reads as an invalid wxDateTime…
wxDateTime ReadDate(const wxVariant& value)
{
	return ibVariantDataSerializable::GetNodeValue(value).AsDate().ToWxDateTime();
}

// …and the value of another: the same property's, named by the cell (ValueToString).
wxVariant WriteDate(const wxVariant& value, const wxDateTime& date)
{
	return wxVariant(new ibVariantDataSerializable(value.GetType(), ibDataValue::Date(ibDateTime::OfWxDateTime(date)),
		wxEmptyString));
}

// -----------------------------------------------------------------------
// ibPGDatePickerEditor
// -----------------------------------------------------------------------

// The grid's picker (wxPGEditor_DatePickerCtrl) over the property's value: it is told the date the node holds, and
// what it was set to goes back as the node.
class ibPGDatePickerEditor : public wxPGEditor {
public:

	virtual wxString GetName() const override { return wxT("ibDatePickerCtrl"); }

	virtual wxPGWindowList CreateControls(wxPropertyGrid* propgrid, wxPGProperty* property, const wxPoint& pos,
		const wxSize& size) const override
	{
		const wxPGWindowList controls = wxPGEditor_DatePickerCtrl->CreateControls(propgrid, property, pos, size);
		UpdateControl(property, controls.GetPrimary());
		return controls;
	}

	virtual void UpdateControl(wxPGProperty* property, wxWindow* wnd) const override
	{
		wxDatePickerCtrl* const ctrl = wxDynamicCast(wnd, wxDatePickerCtrl);
		if (ctrl == nullptr)
			return;
		// An empty date only where the picker can show none (wxDP_ALLOWNONE).
		const wxDateTime date = ReadDate(property->GetValue());
		if (date.IsValid() || ctrl->HasFlag(wxDP_ALLOWNONE))
			ctrl->SetValue(date);
	}

	virtual bool OnEvent(wxPropertyGrid* propgrid, wxPGProperty* property, wxWindow* wnd, wxEvent& event) const override
	{
		return wxPGEditor_DatePickerCtrl->OnEvent(propgrid, property, wnd, event);
	}

	virtual bool GetValueFromControl(wxVariant& variant, wxPGProperty* property, wxWindow* wnd) const override
	{
		wxVariant held;
		if (!wxPGEditor_DatePickerCtrl->GetValueFromControl(held, property, wnd))
			return false;
		variant = WriteDate(property->GetValue(), held.IsNull() ? wxDateTime() : held.GetDateTime());
		return true;
	}

	virtual void SetValueToUnspecified(wxPGProperty* property, wxWindow* wnd) const override
	{
		wxPGEditor_DatePickerCtrl->SetValueToUnspecified(property, wnd);
	}
};

// -----------------------------------------------------------------------
// ibPGDateProperty
// -----------------------------------------------------------------------

// The grid's own cell over the property's value: what it shows and takes is the grid's date, read from the node and
// written back as one.
class ibPGDateProperty : public wxDateProperty {
public:

	ibPGDateProperty(const wxString& label, const wxString& name, const wxVariant& value)
		: wxDateProperty(label, name)
	{
		SetValue(value);
	}

	virtual wxString ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const override
	{
		// No date: nothing, as the desktop's picker with no date reads.
		wxVariant held(ReadDate(value));
		return held.GetDateTime().IsValid() ? wxDateProperty::ValueToString(held, flags) : wxString();
	}

	virtual bool StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const override
	{
		wxVariant held(ReadDate(variant));
		if (!wxDateProperty::StringToValue(held, text, flags))
			return false;
		variant = WriteDate(variant, held.GetDateTime());
		return true;
	}

protected:

	// The picker, registered the first time a date cell is asked for it.
	virtual const wxPGEditor* DoGetEditorClass() const override
	{
		static const wxPGEditor* const editor = wxPropertyGrid::RegisterEditorClass(new ibPGDatePickerEditor());
		return editor;
	}
};

} // namespace

// register frontend property
class ibPropertyDateLoader
{
public:
	ibPropertyDateLoader()
	{
		// ibPropertyDate — the date as the server stores it (a Date node), and back as one.
		ibPropertyRegistry::Register(wxT("ibPropertyDate"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGDateProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
}s_dateLoaderDate;
