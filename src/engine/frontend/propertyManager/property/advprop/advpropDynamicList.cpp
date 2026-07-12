#include "advpropDynamicList.h"

#include "frontend/propertyManager/property/private/prop.h"
#include "frontend/propertyManager/propertyEditor.h"

#include "backend/propertyManager/property/propertyDynamicList.h"

// -----------------------------------------------------------------------
// ibPGDynamicListProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGDynamicListProperty, wxPGProperty, HyperLink)

// register frontend property
class ibPropertyDynamicListLoader
{
public:
	ibPropertyDynamicListLoader()
	{
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(ibPGDynamicListProperty, ibPropertyDynamicList::ms_propertyDynamicList);
	}
}g_dynamicListLoader;

ibPGDynamicListProperty::ibPGDynamicListProperty(ibPropertyObject* ownerProperty, const wxString& label,
	const wxString& name, const wxVariant& value) : wxPGProperty(label, name), m_ownerProperty(ownerProperty) {

	wxPGProperty::SetFlagRecursively(wxPGFlags::ReadOnly, true);
	wxPGProperty::SetFlagRecursively(wxPGFlags::NoEditor, true);

	wxPGProperty::SetValue(wxVariant(false, wxT("hyperLink_clicked")));
}

ibPGDynamicListProperty::~ibPGDynamicListProperty()
{
}

wxString ibPGDynamicListProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	return _("Setup...");
}

bool ibPGDynamicListProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	return false;
}

#include "frontend/visualView/ctrl/formAttribute.h"
#include "frontend/win/dlgs/listSettings/listSettings.h"
#include "backend/system/value/valueDynamicList.h"
#include "backend/composition/listFilter.h"

void ibPGDynamicListProperty::OnSetValue()
{
	if (wxT("hyperLink_clicked") == m_value.GetName() && m_value.GetBool()) {
		// Owner = the property-object that CREATED this "Settings" action: the dynamic
		// list itself (it CreateProperty's its own Settings property; the attribute only
		// copies that property onto itself). So reach the list through the owner directly,
		// type-agnostically - the attribute is never named here (mirrors HyperLink's cast).
		ibValueDynamicList* list = dynamic_cast<ibValueDynamicList*>(m_ownerProperty);
		if (list != nullptr) {
			wxTheApp->CallAfter(
				[list]() {
					// The list is the single source of truth: ShowListSettingsDialog reads
					// its GetSourceQueryable() for the Field list and edits its
					// GetListSettings(); OK applies the change onto the composer.
					ibDialogListSettings::ShowListSettingsDialog(list);
				}
			);
		}
	}
}

void ibPGDynamicListProperty::RefreshChildren()
{
	wxPGProperty::Enable(true);
}
