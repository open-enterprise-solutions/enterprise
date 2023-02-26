////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : form data
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "utils/stringUtils.h"

//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueForm::CValueFormData, CValue);

CValueForm::CValueFormData::CValueFormData() : CValue(eValueTypes::TYPE_VALUE, true),
m_formOwner(NULL), m_methodHelper(NULL)
{
}

CValueForm::CValueFormData::CValueFormData(CValueForm* ownerFrame) : CValue(eValueTypes::TYPE_VALUE, true),
m_formOwner(ownerFrame), m_methodHelper(new CMethodHelper())
{
}

#include "core/compiler/valueMap.h"

CValueForm::CValueFormData::~CValueFormData()
{
	wxDELETE(m_methodHelper);
}

CValue CValueForm::CValueFormData::GetItEmpty()
{
	return new CValueContainer::CValueReturnContainer();
}

CValue CValueForm::CValueFormData::GetItAt(unsigned int idx)
{
	IValueControl* valueControl = NULL;
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
	return new CValueContainer::CValueReturnContainer(
		valueControl->GetControlName(),
		valueControl->GetValue()
	);
}

#include "appData.h"

bool CValueForm::CValueFormData::SetAt(const CValue& varKeyValue, const CValue& varValue)
{
	const number_t& number = varKeyValue.GetNumber();
	if (number.GetUInteger() > Count())
		return false;
	unsigned int count = 0;
	for (auto control : m_formOwner->m_aControls) {
		if (control->HasValueInControl()) {
			count++;
		}
		if (number == count) {
			control->SetControlValue(varValue);
			break;
		}
	}
	return true;
}

bool CValueForm::CValueFormData::GetAt(const CValue& varKeyValue, CValue& pvarValue)
{
	const number_t& number = varKeyValue.GetNumber();
	if (Count() < number.GetUInteger())
		return false;
	unsigned int count = 0;
	for (auto control : m_formOwner->m_aControls) {
		if (control->HasValueInControl()) {
			count++;
		}
		if (number == count) {
			return control->GetControlValue(pvarValue);
		}
	}
	if (!appData->DesignerMode()) 
		CTranslateError::Error("Index goes beyond array");
	return false;
}

bool CValueForm::CValueFormData::Property(const CValue& varKeyValue, CValue& cValueFound)
{
	wxString key = varKeyValue.GetString();
	auto itFounded = std::find_if(m_formOwner->m_aControls.begin(), m_formOwner->m_aControls.end(), [key](IValueControl* control) {
		if (control->HasValueInControl()) return StringUtils::CompareString(key, control->GetControlName()); return false;
		}
	);
	if (itFounded != m_formOwner->m_aControls.end())
		return (*itFounded)->GetControlValue(cValueFound);
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

enum Func {
	enAttributeProperty,
	enAttributeCount
};

void CValueForm::CValueFormData::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	for (auto control : m_formOwner->m_aControls) {
		if (!control->HasValueInControl())
			continue;
		m_methodHelper->AppendProp(
			control->GetControlName(),
			control->GetControlID()
		);
	}
}

#include "frontend/visualView/visualHost.h"

bool CValueForm::CValueFormData::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	unsigned int id = m_methodHelper->GetPropData(lPropNum);
	auto foundedIt = std::find_if(m_formOwner->m_aControls.begin(), m_formOwner->m_aControls.end(),
		[id](IValueFrame* control) {
			return id == control->GetControlID();
		}
	);

	if (foundedIt != m_formOwner->m_aControls.end()) {
		IValueControl* currentControl = *foundedIt;
		wxASSERT(currentControl);
		bool result = currentControl->SetControlValue(varPropVal);
		if (!result)
			return false;
		CVisualDocument* visualDoc = m_formOwner->GetVisualDocument();
		if (!visualDoc)
			return false;
		CVisualHost* visualView = visualDoc->GetVisualView();
		if (!visualView)
			return false;
		wxObject* object = visualView->GetWxObject(currentControl);
		if (object) {
			wxWindow* parentWnd = NULL;
			currentControl->Update(object, visualView);
			IValueFrame* nextParent = currentControl->GetParent();
			while (!parentWnd && nextParent) {
				if (nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW) {
					parentWnd = dynamic_cast<wxWindow*>(visualView->GetWxObject(nextParent));
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
	return true;
}

bool CValueForm::CValueFormData::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	unsigned int id = m_methodHelper->GetPropData(lPropNum);
	auto foundedIt = std::find_if(m_formOwner->m_aControls.begin(), m_formOwner->m_aControls.end(),
		[id](IValueFrame* control) {
			return id == control->GetControlID();
		}
	);
	if (foundedIt != m_formOwner->m_aControls.end()) {
		IValueControl* currentControl = *foundedIt;
		wxASSERT(currentControl);
		return currentControl->GetControlValue(pvarPropVal);
	}
	return false;
}

bool CValueForm::CValueFormData::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enAttributeProperty:
		pvarRetValue = Property(*paParams[0], lSizeArray > 1 ? *paParams[1] : CValue());
		return true;
	case enAttributeCount:
		pvarRetValue = Count();
		return true;
	}

	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SO_VALUE_REGISTER(CValueForm::CValueFormData, "formData", TEXT2CLSID("VL_FDTA"));