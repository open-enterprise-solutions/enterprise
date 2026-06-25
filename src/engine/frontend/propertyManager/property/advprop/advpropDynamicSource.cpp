#include "advpropDynamicSource.h"

#include "frontend/propertyManager/property/private/prop.h"
#include "frontend/propertyManager/propertyEditor.h"

#include "backend/propertyManager/property/propertyDynamicSource.h"            // ms_propertyDynamicSource slot
#include "backend/propertyManager/property/variant/variantDynamicSource.h"     // ibVariantDataDynamicSource
#include "backend/metaCollection/partial/list/dynamicList.h"                   // ibValueDynamicList (the owner)
#include "backend/appData.h"
#include "backend/query/queryableFactory.h"

#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/button.h>

// -----------------------------------------------------------------------
// ibPGDynamicSourceProperty — a ComboBoxAndButton for the dynamic-list "Source".
// The drop-down lists every registered source (queryable factory); the "..." button
// opens the same list as a dialog. Either way the pick is applied through the owner
// list facade (ibValueDynamicList::SetSource) so the source change is gated and the
// columns / settings are rebuilt once. Modelled on ibPGTypeProperty.
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGDynamicSourceProperty, wxPGProperty, ComboBoxAndButton)

// register frontend property
class ibPropertyDynamicSourceLoader
{
public:
	ibPropertyDynamicSourceLoader()
	{
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(ibPGDynamicSourceProperty, ibPropertyDynamicSource::ms_propertyDynamicSource);
	}
}g_dynamicSourceLoader;

ibPGDynamicSourceProperty::ibPGDynamicSourceProperty(ibPropertyObject* property, const wxString& label,
	const wxString& name, const wxVariant& value) : wxPGProperty(label, name), m_ownerProperty(property)
{
	m_flags |= wxPGPropertyFlags_ActiveButton;   // the "..." button is always enabled

	// Fill the drop-down with every registered source (namespace.name) — straight from the
	// factory. m_sources is kept index-aligned so IntToValue maps the pick back to identity.
	if (ibQueryableFactory* factory = ibApplicationData::GetQueryableFactory()) {
		for (ibQueryableSourceDescriptor* d : factory->GetDescriptors()) {
			if (d == nullptr)
				continue;
			m_choices.Add(d->GetNamespace() + wxT(".") + d->GetName());
			m_sources.emplace_back(d->GetNamespace(), d->GetName());
		}
	}

	SetValue(value);
}

ibPGDynamicSourceProperty::~ibPGDynamicSourceProperty()
{
}

wxString ibPGDynamicSourceProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	// The variant's representation (ibVariantDataDynamicSource::Write -> MakeString): the
	// source name, or empty when no source is chosen yet.
	return value.MakeString();
}

bool ibPGDynamicSourceProperty::StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const
{
	return false;
}

bool ibPGDynamicSourceProperty::IntToValue(wxVariant& value, int number, wxPGPropValFormatFlags flags) const
{
	// The owner of the "Source" property IS the dynamic list (it created the property).
	ibValueDynamicList* list = dynamic_cast<ibValueDynamicList*>(m_ownerProperty);
	if (list == nullptr || number < 0 || number >= (int)m_sources.size())
		return false;
	// Facade: gates on a REAL source change, rebuilds columns/composer and re-applies the
	// settings. Yield the new variant so the backend property value stays in sync.
	list->SetSource(m_sources[number].first, m_sources[number].second);
	value = new ibVariantDataDynamicSource(list->GetSourceQueryable());
	return true;
}

wxPGEditorDialogAdapter* ibPGDynamicSourceProperty::GetEditorDialog() const
{
	class wxPGDynamicSourceAdapter : public wxPGEditorDialogAdapter {
	public:
		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) override {
			ibPGDynamicSourceProperty* dlgProp = wxDynamicCast(prop, ibPGDynamicSourceProperty);
			if (dlgProp == nullptr)
				return false;
			ibValueDynamicList* list = dynamic_cast<ibValueDynamicList*>(dlgProp->GetOwnerProperty());
			if (list == nullptr)
				return false;
			ibQueryableFactory* factory = ibApplicationData::GetQueryableFactory();
			if (factory == nullptr)
				return false;

			wxDialog dlg(pg, wxID_ANY, _("Select source"), wxDefaultPosition, wxSize(400, 320),
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
			wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
			wxListBox* listBox = new wxListBox(&dlg, wxID_ANY);
			sizer->Add(listBox, 1, wxALL | wxEXPAND, 8);
			sizer->Add(dlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxALIGN_RIGHT, 8);
			dlg.SetSizer(sizer);

			// Options = every registered source (namespace.name) — straight from the factory.
			const ibBackendQueryable* current = list->GetSourceQueryable();
			std::vector<std::pair<wxString, wxString>> sources;   // (ns, name)
			for (ibQueryableSourceDescriptor* d : factory->GetDescriptors()) {
				if (d == nullptr)
					continue;
				const int idx = listBox->Append(d->GetNamespace() + wxT(".") + d->GetName());
				sources.emplace_back(d->GetNamespace(), d->GetName());
				// Pre-select the source already chosen on the list (re-open lands on it).
				if (current != nullptr && current == factory->Resolve(d->GetNamespace(), d->GetName()))
					listBox->SetSelection(idx);
			}

			if (dlg.ShowModal() != wxID_OK)
				return false;

			const int sel = listBox->GetSelection();
			if (sel < 0 || sel >= (int)sources.size())
				return false;

			// Facade: stores the queryable into the property variant AND rebuilds (gated on a
			// real change). Commit the new variant through the grid so the cell repaints and
			// the backend property value stays in sync.
			list->SetSource(sources[sel].first, sources[sel].second);
			SetValue(new ibVariantDataDynamicSource(list->GetSourceQueryable()));
			return true;
		}
	};
	return new wxPGDynamicSourceAdapter();
}
