#include "advpropHyperLink.h"

#include "frontend/propertyManager/property/private/prop.h"
#include "frontend/propertyManager/propertyEditor.h"

// -----------------------------------------------------------------------
// ibPGHyperLinkProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGHyperLinkProperty, wxPGProperty, HyperLink)

#include "backend/metaCollection/metaObject.h"

ibPGHyperLinkProperty::ibPGHyperLinkProperty(ibPropertyObject* ownerProperty, const wxString& label,
	const wxString& name, const wxVariant& value) : wxPGProperty(label, name), m_ownerProperty(ownerProperty) {

	wxPGProperty::SetFlagRecursively(wxPGFlags::ReadOnly, true);
	//wxPGProperty::SetFlagRecursively(wxPGFlags::Hidden, true);
	wxPGProperty::SetFlagRecursively(wxPGFlags::NoEditor, true);

	wxPGProperty::SetValue(wxVariant(false, wxT("hyperLink_clicked")));
}

ibPGHyperLinkProperty::~ibPGHyperLinkProperty()
{
}

wxString ibPGHyperLinkProperty::ValueToString( wxVariant& value, wxPGPropValFormatFlags flags ) const
{
	return _("Open");
}

bool ibPGHyperLinkProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	return false;
}

#include "backend/metaData.h"

void ibPGHyperLinkProperty::OnSetValue()
{
	if (wxT("hyperLink_clicked") == m_value.GetName() && m_value.GetBool()) {
		ibValueMetaObject* metaObject = dynamic_cast<ibValueMetaObject*>(m_ownerProperty);
		if (metaObject != nullptr) {
			wxTheApp->CallAfter(
				[metaObject]() {
					ibMetaData* metaData = metaObject->GetMetaData();
					if (metaData != nullptr) {
						ibBackendMetadataTree* metaTree = metaData->GetMetaTree();
						if (metaTree != nullptr) metaTree->OpenObjectForm(metaObject);
					}
				}
			);
		}
	}
}

void ibPGHyperLinkProperty::RefreshChildren()
{
	wxPGProperty::Enable(true);
}
