////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : form data
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "compiler/methods.h"
#include "utils/stringUtils.h"

//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueForm::CValueFormData, CValue);

CValueForm::CValueFormData::CValueFormData() : CValue(eValueTypes::TYPE_VALUE, true),
m_formOwner(NULL), m_methods(NULL)
{
}

CValueForm::CValueFormData::CValueFormData(CValueForm *ownerFrame) : CValue(eValueTypes::TYPE_VALUE, true),
m_formOwner(ownerFrame), m_methods(new CMethods())
{
}

#include "compiler/valueMap.h"

CValueForm::CValueFormData::~CValueFormData()
{
	wxDELETE(m_methods);
}

CValue CValueForm::CValueFormData::GetItEmpty()
{
	return new CValueContainer::CValueReturnContainer();
}

CValue CValueForm::CValueFormData::GetItAt(unsigned int idx)
{
	IValueControl *valueControl = NULL; 

	if (m_formOwner->m_aControls.size() < idx) {
		return CValue();
	}

	unsigned int count = 0;

	for (auto control : m_formOwner->m_aControls) {
		if (control->HasValueInControl()) {
			count++;
		}
		if (idx == count) {
			valueControl = control;
			break;
		}
	}

	wxASSERT(valueControl);

	wxString propertyName;
	if (!valueControl->GetPropertyValue("name", propertyName)) {
		return CValue();
	}

	return new CValueContainer::CValueReturnContainer(propertyName, 
		CValue(valueControl)
	);
}

#include "appData.h"

void CValueForm::CValueFormData::SetAt(const CValue &cKey, CValue &cVal)
{
	number_t number = cKey.GetNumber();
	if (number.ToUInt() > Count())
		return;

	unsigned int count = 0;

	for (auto control : m_formOwner->m_aControls) {
		if (control->HasValueInControl()) {
			count++;
		}
		if (number == count) {
			control->SetControlValue(cVal);
			break;
		}
	}
}

CValue CValueForm::CValueFormData::GetAt(const CValue &cKey)
{
	number_t number = cKey.GetNumber();

	if (Count() < number.ToUInt())
		return CValue();

	unsigned int count = 0;

	for (auto control : m_formOwner->m_aControls) {
		if (control->HasValueInControl()) {
			count++;
		}
		if (number == count) {
			return control->GetControlValue();
		}
	}

	if (!appData->DesignerMode()) {
		CTranslateError::Error(_("Index goes beyond array"));
	}

	return CValue();
}

bool CValueForm::CValueFormData::Property(const CValue &cKey, CValue &cValueFound)
{
	wxString key = cKey.GetString();
	auto itFounded = std::find_if(m_formOwner->m_aControls.begin(), m_formOwner->m_aControls.end(), [key](IValueControl *control) { 
		if (control->HasValueInControl()) {
			return StringUtils::CompareString(key, control->GetPropertyAsString("name"));
		}
		return false; 
	});
	
	if (itFounded != m_formOwner->m_aControls.end()) {
		cValueFound = (*itFounded)->GetControlValue(); 
		return true; 
	}
	return false;
}

unsigned int CValueForm::CValueFormData::Count() const
{
	unsigned int count = 0;
	for (auto control : m_formOwner->m_aControls) {
		if (control->HasValueInControl()) {
			count++;
		}
	}
	return count;
}

enum
{
	enAttributeProperty,
	enAttributeCount
};

void CValueForm::CValueFormData::PrepareNames() const
{
	std::vector<SEng> aMethods = {
	{"property", "property(key, valueFound)"},
	{"count", "count()"}
	};

	m_methods->PrepareMethods(aMethods.data(), aMethods.size());

	std::vector<SEng> attributes;

	for (auto control : m_formOwner->m_aControls) {
		if (!control->HasValueInControl())
			continue;
		
		SEng attr;
		attr.sName = control->GetPropertyAsString("name");
		attr.iName = control->GetControlID();
		attributes.push_back(attr);
	}

	m_methods->PrepareAttributes(attributes.data(), attributes.size());
}

#include "frontend/visualView/visualEditorView.h"

void CValueForm::CValueFormData::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
	unsigned int id = m_methods->GetAttributePosition(aParams.GetIndex());

	auto foundedIt = std::find_if(m_formOwner->m_aControls.begin(), m_formOwner->m_aControls.end(),
		[id](IValueFrame *control) { return id == control->GetControlID(); });

	if (foundedIt != m_formOwner->m_aControls.end()) {
		IValueControl *currentControl = *foundedIt; 
		wxASSERT(currentControl);
		currentControl->SetControlValue(cVal);
		CVisualDocument *visualDoc = m_formOwner->GetVisualDocument();
		if (!visualDoc)
			return;
		CVisualView *visualView = visualDoc->GetVisualView();
		if (!visualView)
			return;
		wxObject *object = visualView->GetWxObject(currentControl);
		if (object) {
			wxWindow *parentWnd = NULL;
			currentControl->Update(object, visualView);
			IValueFrame* nextParent = currentControl->GetParent();
			while (!parentWnd && nextParent) {
				if (nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW) {
					parentWnd = dynamic_cast<wxWindow *>(visualView->GetWxObject(nextParent));
					break;
				}
				nextParent = nextParent->GetParent();
			}
			if (!parentWnd) {
				parentWnd = visualView->GetBackgroundWindow();
			}
			currentControl->OnUpdated(object, parentWnd, visualView);
		}
	}
}

CValue CValueForm::CValueFormData::GetAttribute(attributeArg_t &aParams)
{
	unsigned int id = m_methods->GetAttributePosition(aParams.GetIndex());

	auto foundedIt = std::find_if(m_formOwner->m_aControls.begin(), m_formOwner->m_aControls.end(),
		[id](IValueFrame *control) { return id == control->GetControlID(); });

	if (foundedIt != m_formOwner->m_aControls.end()) {
		IValueControl *currentControl = *foundedIt;
		wxASSERT(currentControl);
		return currentControl->GetControlValue();
	}
	return CValue();
}

CValue CValueForm::CValueFormData::Method(methodArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case enAttributeProperty:
		return Property(aParams[0], aParams.GetParamCount() > 1 ? aParams[1] : CValue());
	case enAttributeCount:
		return Count();
	}

	return CValue();
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SO_VALUE_REGISTER(CValueForm::CValueFormData, "formData", CValueFormData, TEXT2CLSID("VL_FDTA"));