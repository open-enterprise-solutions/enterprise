#include "deskgui/univ/propgrid/advprops.h"
#include "deskgui/univ/propgrid/propgrid.h"   // ibPropertyGrid::RegisterEditorClass
#include "deskgui/univ/ctrl/datePickerCtrl.h"   // ibDatePickerCtrl — the grid's picker, told the date

#include "frmclient/backend/backend_core.h"   // ibDateTime — the date as the server stores it
#include "deskgui/typeconv.h"   // DateTimeToWxDateTime / WxDateTimeToDateTime — a date across to wx and back
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the date's node
#include "frmclient/propertyManager/property/private/prop.h"             // ibPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

namespace {

// The date a value holds, as the server stores it (a Date node); the empty date reads as an invalid wxDateTime…
wxDateTime ReadDate(const ibVariant& value)
{
	return typeConv::DateTimeToWxDateTime(ibVariantSerializable::GetNodeValue(value).AsDate());
}

// …and the value of another: the same property's, named by the cell (ValueToString).
ibVariant WriteDate(const wxDateTime& date)
{
	return ibVariant(new ibVariantSerializable(ibDataValue::Date(typeConv::WxDateTimeToDateTime(date)),
		wxEmptyString));
}

// -----------------------------------------------------------------------
// ibPGDatePickerEditor
// -----------------------------------------------------------------------

// The grid's picker (ibPGEditor_DatePickerCtrl) over the property's value: it is told the date the node holds, and
// what it was set to goes back as the node.
class ibPGDatePickerEditor : public ibPGEditor {
public:

	virtual wxString GetName() const override { return wxT("ibDatePickerCtrl"); }

	virtual ibPGWindowList CreateControls(ibPropertyGrid* propgrid, ibPGProperty* property, const wxPoint& pos,
		const wxSize& size) const override
	{
		const ibPGWindowList controls = ibPGEditor_DatePickerCtrl->CreateControls(propgrid, property, pos, size);
		UpdateControl(property, controls.GetPrimary());
		return controls;
	}

	virtual void UpdateControl(ibPGProperty* property, wxWindow* wnd) const override
	{
		ibDatePickerCtrl* const ctrl = wxDynamicCast(wnd, ibDatePickerCtrl);
		if (ctrl == nullptr)
			return;
		// An empty date only where the picker can show none (wxDP_ALLOWNONE).
		const wxDateTime date = ReadDate(property->GetValue());
		if (date.IsValid() || ctrl->HasFlag(wxDP_ALLOWNONE))
			ctrl->SetValue(date);
	}

	virtual bool OnEvent(ibPropertyGrid* propgrid, ibPGProperty* property, wxWindow* wnd, wxEvent& event) const override
	{
		return ibPGEditor_DatePickerCtrl->OnEvent(propgrid, property, wnd, event);
	}

	virtual bool GetValueFromControl(ibVariant& variant, ibPGProperty* property, wxWindow* wnd) const override
	{
		ibVariant held;
		if (!ibPGEditor_DatePickerCtrl->GetValueFromControl(held, property, wnd))
			return false;
		variant = WriteDate(held.GetType() == ibValueTypes::TYPE_EMPTY ? wxDateTime() : typeConv::DateTimeToWxDateTime(held.GetDate()));
		return true;
	}

	virtual void SetValueToUnspecified(ibPGProperty* property, wxWindow* wnd) const override
	{
		ibPGEditor_DatePickerCtrl->SetValueToUnspecified(property, wnd);
	}
};

// -----------------------------------------------------------------------
// ibPGDateProperty
// -----------------------------------------------------------------------

// The grid's own cell over the property's value: what it shows and takes is the grid's date, read from the node and
// written back as one.
class ibPGDateProperty : public ibDateProperty {
public:

	ibPGDateProperty(const wxString& label, const wxString& name, const ibVariant& value)
		: ibDateProperty(label, name)
	{
		SetValue(value);
	}

	virtual wxString ValueToString(ibVariant& value, ibPGPropValFormatFlags flags) const override
	{
		// No date: nothing, as the desktop's picker with no date reads.
		ibVariant held = IBVARIANT(ReadDate(value));
		return typeConv::DateTimeToWxDateTime(held.GetDate()).IsValid() ? ibDateProperty::ValueToString(held, flags) : wxString();
	}

	virtual bool StringToValue(ibVariant& variant, const wxString& text, ibPGPropValFormatFlags flags) const override
	{
		ibVariant held = IBVARIANT(ReadDate(variant));
		if (!ibDateProperty::StringToValue(held, text, flags))
			return false;
		variant = WriteDate(typeConv::DateTimeToWxDateTime(held.GetDate()));
		return true;
	}

protected:

	// The picker, registered the first time a date cell is asked for it.
	virtual const ibPGEditor* DoGetEditorClass() const override
	{
		static const ibPGEditor* const editor = ibPropertyGrid::RegisterEditorClass(new ibPGDatePickerEditor());
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
		ibPropertyRegistry::Register(wxT("ibPropertyDate"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibPGDateProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
}s_dateLoaderDate;
