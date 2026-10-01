#include "advpropDataComposition.h"

#include "backend/propertyManager/property/propertyDataComposition.h"
#include "backend/propertyManager/property/propertyComposition.h"   // the composer metatype's own — editor-less
#include "backend/propertyManager/property/propertyDynamicList.h"   // the OTHER registration — same cell, other window
#include "backend/propertyManager/property/variant/variantComposition.h"
#include "backend/compositionDescription.h"                          // what a door is handed

#include "frontend/win/dlgs/settings/composer/composerSettings.h"   // one door…
#include "frontend/win/dlgs/settings/list/listSettings.h"           // …and the other — see the two registrations
#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_* — the grid flags
#include "frontend/propertyManager/property/private/propertyRegistry.h"
#include "frontend/propertyManager/propertyEditor.h"
// -----------------------------------------------------------------------
// ibPGDataCompositionProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGDataCompositionProperty, wxPGProperty, HyperLink)

// register frontend property — matched by the BACKEND type, which is why the two
// settings actions need no branch of their own.
class ibPropertyDataCompositionLoader
{
public:
	ibPropertyDataCompositionLoader()
	{
		// ⭐ TWO REGISTRATIONS, ONE CELL — they differ in the WINDOW, which is an argument now.
		//
		// A COMPOSITION on a form attribute opens the composer's window: outputs, resources,
		// parameters, the query.
		ibPropertyRegistry::Register([](ibPropertyDataComposition* prop) -> wxPGProperty* {
			return new ibPGDataCompositionProperty(prop->GetPropertyObject(), prop->GetLabel(),
				prop->GetName(), prop->GetValue(),
				[](ibCompositionDescription& desc, const ibMetaData* metaData) {
					return ibDialogComposerSettings::ShowComposerSettings(nullptr, desc, metaData);
				});
		});

		// …and a DYNAMIC LIST opens the list's: filter, sort, grouping and its query. It had a cell
		// class of its own — 158 lines identical to this one but for the line above.
		ibPropertyRegistry::Register([](ibPropertyDynamicList* prop) -> wxPGProperty* {
			return new ibPGDataCompositionProperty(prop->GetPropertyObject(), prop->GetLabel(),
				prop->GetName(), prop->GetValue(),
				[](ibCompositionDescription& desc, const ibMetaData* metaData) {
					return ibDialogListSettings::ShowListSettings(desc, metaData);
				});
		});

		// Editor-less BY DESIGN — the OTHER composition property, the one the composer metatype
		// keeps its content in. It carries the composition itself, and the composition is edited in
		// the composer's own window, never in a grid cell. Declared here so the inspector answers
		// null quietly instead of asserting on a Register nobody forgot.
		//
		// 🛑 AND IT IS `ibPropertyComposition`, NOT `ibPropertyDataComposition`. This line named the
		// wrong one of the pair — the type that HAS an editor (registered just above), while the one
		// that needs the silence stayed unregistered. Selecting a Composer in the report tree then hit
		// the registry's "nobody taught the inspector to draw this" guard and took the designer down
		// (dump designer_3836, 2026-08-24). The comment above was right about which type it meant; the
		// template argument was not.
		ibPropertyRegistry::RegisterNoEditor<ibPropertyComposition>();
	}
}g_dataCompositionLoader;

ibPGDataCompositionProperty::ibPGDataCompositionProperty(const ibPropertyObject* property, const wxString& label,
	const wxString& name, const wxVariant& value, ibSettingsDoor door)
	: wxPGProperty(label, name), m_property(property), m_door(std::move(door)) {

	wxPGProperty::SetFlagRecursively(wxPGFlags::ReadOnly, true);
	wxPGProperty::SetFlagRecursively(wxPGFlags::NoEditor, true);

	// ⭐ THE CELL'S VALUE IS THE VARIANT IT WAS GIVEN — `value` already carries an
	// ibVariantDataComposition (Max, 2026-08-24). It used to be thrown away here and replaced by the
	// hyperlink's click flag, which left the cell with nothing to clone from and nothing to set back.
	// The flag still arrives on a click; it is transient, and OnSetValue puts a real variant back.
	wxPGProperty::SetValue(value);
}

ibPGDataCompositionProperty::~ibPGDataCompositionProperty()
{
}

wxString ibPGDataCompositionProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	return _("Setup...");
}

bool ibPGDataCompositionProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	return false;
}

#include "frontend/mainFrame/mainFrame.h"

void ibPGDataCompositionProperty::OnSetValue()
{
	if (wxT("hyperLink_clicked") == m_value.GetName()) {

		// ⭐⭐ THE CLICK IS TAKEN, SO IT IS ENDED. The name is a signal — "somebody just clicked" — and
		// a signal left written on the value has become a state saying "clicked" forever: OnSetValue
		// is not a one-shot, so the next refresh reads the same name and opens another window, and
		// another (Max, 2026-08-24: "this state is true forever, it has to be cleared").
		//
		// Cleared HERE, by the reader that answers it, rather than by the editor that struck it: the
		// editor cannot know when the click has been dealt with, and a second SetValue there runs
		// every OTHER cell's handler a second time inside the same click.
		//
		// The name only — the composition itself is untouched, which is the whole point of stamping a
		// name instead of writing a flag into the payload.
		m_value.SetName(wxEmptyString);

		ibVariantDataComposition* composer = property_cast(m_value, ibVariantDataComposition);
		if (composer != nullptr) {

			// ⭐⭐ HERE, NOT DEFERRED — because THIS is inside the editor's event, and that is the only
			// place a pending value is collected: wxPropertyGrid::HandleCustomEditorEvent asks
			// WasValueChangedInEvent() right after the editor's OnEvent, then validates and runs
			// DoPropertyChanged (propgrid.cpp). A CallAfter lands past that window, and the value it
			// posts is picked up by nobody — which is exactly what the journal showed.
			{
				// ⭐ THE CLONE IS HELD BY A VARIANT FROM THE MOMENT IT EXISTS. Clone() hands back a
				// raw wxVariantData*, and there are two ways out of here — refused, or set. A variant
				// owns what it is given, so the refused way releases it instead of leaking
				// (Max, 2026-08-24: "somebody has to hold it until the value is set").
				ibVariantDataComposition* clone = composer->Clone();
				const wxVariant held(clone);   // the holder — no cast needed, Clone is already typed

				// WHICHEVER WINDOW THIS CELL WAS REGISTERED WITH — it edits the description the
				// variant carries, in place. The configuration its names mean is asked of the
				// variant's owner THROUGH A CONST POINTER, because the const GetMetaData is the
				// overload types implement; the non-const one is a guarded wxFAIL. Null is legitimate.
				if (!m_door || !m_door(clone->GetCompositionDesc(),
					m_property != nullptr ? m_property->GetMetaData() : nullptr))
					return;

				// ⭐⭐ THE VALUE FIRST, THEN THE PROPERTY IS TOLD — and the property is what raises
				// modified-ness and starts the cascade (Max, 2026-08-24). Not a grid door: `SetValue`
				// announces nothing at all, and `SetValueInEvent` hands the grid a pending value that
				// only editor-event processing collects — we are past that, in a CallAfter, so it was
				// collected by nobody (both measured in the journal today).
				//
				// OnChildChanged is the road that already exists and already works — the dynamic list
				// travels it: it bubbles up the attach chain, payload-free, to the form-attribute
				// holder, which marks the DOCUMENT modified and refreshes the editor.
				//
				// ⭐⭐ POSTED AS THE PENDING VALUE, and the grid does the rest — validate, commit,
				// announce. That announcement is what ibObjectInspector::ModifyProperty listens to,
				// and it is the one place the value reaches the BACKEND property, the child raises
				// its own change, and the form is marked modified. Nothing of that belongs here
				// (Max, 2026-08-24: "it had to be driven through SetValueInEvent").
				SetValueInEvent(held);
			}
		}
	}
}

void ibPGDataCompositionProperty::RefreshChildren()
{
	wxPGProperty::Enable(true);
}
