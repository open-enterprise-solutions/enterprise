#include "advpropEvent.h"
#include "advpropValuePicture.h"                                         // the value's picture, read when drawn

#include "core/stringUtils.h"
#include "frmclient/backend/backend_picture.h"      // ibBackendPicture::GetPicture
#include "core/picturePredefined.h"    // g_picEventCLSID — an event's picture

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the event's node
#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// -----------------------------------------------------------------------
// ibPGEventProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGEventProperty, wxStringProperty, TextCtrlAndButton)

namespace {

// The handler a value names — its node a child: the event's Name, the handler's Value…
wxString ReadHandler(const wxVariant& value)
{
	const auto node = ibVariantDataSerializable::GetNodeValue(value).AsChild();
	return node ? node->GetValue<ibString>(ibT("Value")) : ibString();
}

// …and the value naming another: the same event's (its name is its own), read as the handler.
wxVariant WriteHandler(const wxVariant& value, const wxString& handler)
{
	const auto held = ibVariantDataSerializable::GetNodeValue(value).AsChild();
	auto node = held ? std::make_shared<ibDataNode>(*held) : std::make_shared<ibDataNode>();
	node->SetValue(wxT("Value"), ibString(handler));
	return wxVariant(new ibVariantDataSerializable(value.GetType(), ibDataValue::Child(node), handler));
}

} // namespace

// register frontend property
class ibPropertyEventLoader
{
public:
	ibPropertyEventLoader()
	{
		// The server's ibEventControl — its node a child: the event's Name, the handler's Value; the handler goes back
		// alone (the name is the event's own).
		ibPropertyRegistry::Register(wxT("ibEventControl"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGEventProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
} g_eventLoader;

ibPGEventProperty::ibPGEventProperty(const wxString& label, const wxString& name, const wxVariant& value)
	: wxStringProperty(label, name)
{
	m_flags |= wxPGPropertyFlags_ActiveButton; // Property button always enabled.
	SetValue(value);
}

// An event, as an event handler's Event list shows one — answered when the grid draws (advpropValuePicture.h).
wxSize ibPGEventProperty::OnMeasureImage(int /*item*/) const
{
	return ibMeasureValuePicture(ibBackendPicture::GetPicture(g_picEventCLSID), GetGrid());
}

void ibPGEventProperty::OnCustomPaint(wxDC& dc, const wxRect& rect, wxPGPaintData& paintdata)
{
	ibPaintValuePicture(ibBackendPicture::GetPicture(g_picEventCLSID), dc, rect, paintdata);
}

wxString ibPGEventProperty::ValueToString(wxVariant& value,
	wxPGPropValFormatFlags flags) const
{
	wxString s = ReadHandler(value);

	if (HasAnyChild() && HasFlag(wxPGFlags::ComposedValue))
	{
		// Value stored in m_value is non-editable, non-full value
		if (!!(flags & wxPGPropValFormatFlags::FullValue) ||
			!!(flags & wxPGPropValFormatFlags::EditableValue) ||
			s.empty())
		{
			// Calling this under incorrect conditions will fail
			wxASSERT_MSG(!!(flags & wxPGPropValFormatFlags::ValueIsCurrent),
				wxS("Sorry, currently default wxPGProperty::ValueToString() ")
				wxS("implementation only works if value is m_value."));

			DoGenerateComposedValue(s, flags);
		}

		return s;
	}

	// If string is password and value is for visual purposes,
	// then return asterisks instead the actual string.
	if (!!(m_flags & wxPGPropertyFlags_Password) && !(flags & (wxPGPropValFormatFlags::FullValue | wxPGPropValFormatFlags::EditableValue)))
		return wxString(wxS('*'), s.length());

	return s;
}

bool ibPGEventProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	if (stringUtils::CheckCorrectName(text) > 0)
		return false;

	if (GetChildCount() && HasFlag(wxPGFlags::ComposedValue))
		return wxPGProperty::StringToValue(variant, text, flags);

	if (ReadHandler(variant) != text)
	{
		variant = WriteHandler(variant, text);
		return true;
	}

	return false;
}

#include <wx/propgrid/advprops.h>

wxPGEditorDialogAdapter* ibPGEventProperty::GetEditorDialog() const
{
	class ibPGEditorRecordDialogAdapter : public wxPGEditorDialogAdapter {
	public:

		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
		{
			ibPGEventProperty* dlgProp = wxDynamicCast(prop, ibPGEventProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");
			const wxVariant uncommitted = pg->GetUncommittedPropertyValue();
			if (ReadHandler(uncommitted).IsEmpty()) {

				wxPGProperty* pgProp = pg->GetPropertyByName(wxT("Name"));
				prop->SetValueFromString(
					(pgProp ? pgProp->GetDisplayedString() : wxString()) + prop->GetName());

				SetValue(prop->GetValue());
			}
			else {
				SetValue(uncommitted);
			}

			return true;
		}
	};

	return new ibPGEditorRecordDialogAdapter();
}
