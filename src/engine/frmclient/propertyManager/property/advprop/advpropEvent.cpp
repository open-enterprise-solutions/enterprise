#include "advpropEvent.h"
#include "advpropValuePicture.h"                                         // the value's picture, read when drawn

#include "core/stringUtils.h"
#include "frmclient/backend/backend_picture.h"      // ibBackendPicture::GetPicture
#include "core/picturePredefined.h"    // g_picEventCLSID — an event's picture

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the event's node
#include "frmclient/propertyManager/property/private/prop.h"             // ibPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// -----------------------------------------------------------------------
// ibPGEventProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGEventProperty, ibStringProperty, TextCtrlAndButton)

namespace {

// The handler a value names — its node a child: the event's Name, the handler's Value…
wxString ReadHandler(const ibVariant& value)
{
	const auto node = ibVariantSerializable::GetNodeValue(value).AsChild();
	return node ? node->GetValue<ibString>(ibT("Value")) : ibString();
}

// …and the value naming another: the same event's (its name is its own), read as the handler — put in place of the one
// it held: SetValue appends, and a second Value went to the server behind the first, empty one it read (the handler
// typed was never kept).
ibVariant WriteHandler(const ibVariant& value, const wxString& handler)
{
	const auto held = ibVariantSerializable::GetNodeValue(value).AsChild();
	auto node = held ? std::make_shared<ibDataNode>(*held) : std::make_shared<ibDataNode>();
	node->SetField(ibT("Value"), ibDataValue::String(ibString(handler)));
	return ibVariant(new ibVariantSerializable(ibDataValue::Child(node), handler));
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
		ibPropertyRegistry::Register(wxT("ibEventControl"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibPGEventProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
} g_eventLoader;

ibPGEventProperty::ibPGEventProperty(const wxString& label, const wxString& name, const ibVariant& value)
	: ibStringProperty(label, name)
{
	m_flags |= ibPGPropertyFlags_ActiveButton; // Property button always enabled.
	SetValue(value);
}

// An event, as an event handler's Event list shows one — answered when the grid draws (advpropValuePicture.h).
wxSize ibPGEventProperty::OnMeasureImage(int /*item*/) const
{
	return ibMeasureValuePicture(ibBackendPicture::GetPicture(g_picEventCLSID), GetGrid());
}

void ibPGEventProperty::OnCustomPaint(wxDC& dc, const wxRect& rect, ibPGPaintData& paintdata)
{
	ibPaintValuePicture(ibBackendPicture::GetPicture(g_picEventCLSID), dc, rect, paintdata);
}

wxString ibPGEventProperty::ValueToString(ibVariant& value,
	ibPGPropValFormatFlags flags) const
{
	wxString s = ReadHandler(value);

	if (HasAnyChild() && HasFlag(ibPGFlags::ComposedValue))
	{
		// Value stored in m_value is non-editable, non-full value
		if (!!(flags & ibPGPropValFormatFlags::FullValue) ||
			!!(flags & ibPGPropValFormatFlags::EditableValue) ||
			s.empty())
		{
			// Calling this under incorrect conditions will fail
			wxASSERT_MSG(!!(flags & ibPGPropValFormatFlags::ValueIsCurrent),
				wxS("Sorry, currently default ibPGProperty::ValueToString() ")
				wxS("implementation only works if value is m_value."));

			DoGenerateComposedValue(s, flags);
		}

		return s;
	}

	// If string is password and value is for visual purposes,
	// then return asterisks instead the actual string.
	if (!!(m_flags & ibPGPropertyFlags_Password) && !(flags & (ibPGPropValFormatFlags::FullValue | ibPGPropValFormatFlags::EditableValue)))
		return wxString(wxS('*'), s.length());

	return s;
}

bool ibPGEventProperty::StringToValue(ibVariant& variant,
	const wxString& text,
	ibPGPropValFormatFlags flags) const
{
	if (stringUtils::CheckCorrectName(text) > 0)
		return false;

	if (GetChildCount() && HasFlag(ibPGFlags::ComposedValue))
		return ibPGProperty::StringToValue(variant, text, flags);

	if (ReadHandler(variant) != text)
	{
		variant = WriteHandler(variant, text);
		return true;
	}

	return false;
}

#include "deskgui/univ/propgrid/advprops.h"

ibPGEditorDialogAdapter* ibPGEventProperty::GetEditorDialog() const
{
	class ibPGEditorRecordDialogAdapter : public ibPGEditorDialogAdapter {
	public:

		virtual bool DoShowDialog(ibPropertyGrid* pg, ibPGProperty* prop) wxOVERRIDE
		{
			ibPGEventProperty* dlgProp = wxDynamicCast(prop, ibPGEventProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");
			const ibVariant uncommitted = pg->GetUncommittedPropertyValue();
			if (ReadHandler(uncommitted).IsEmpty()) {

				ibPGProperty* pgProp = pg->GetPropertyByName(wxT("Name"));
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
