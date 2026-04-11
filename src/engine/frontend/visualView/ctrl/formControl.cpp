////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : form - controls 
////////////////////////////////////////////////////////////////////////////

#include "form.h"


//////////////////////////////////////////////////////////////////////
wxIMPLEMENT_DYNAMIC_CLASS(ibValueForm::ibValueFormCollectionControl, ibValue);
//////////////////////////////////////////////////////////////////////

ibValueForm::ibValueFormCollectionControl::ibValueFormCollectionControl() : ibValue(ibValueTypes::TYPE_VALUE, true),
m_formOwner(nullptr), m_methodHelper(nullptr)
{
}

ibValueForm::ibValueFormCollectionControl::ibValueFormCollectionControl(ibValueForm* ownerFrame) : ibValue(ibValueTypes::TYPE_VALUE, true),
m_formOwner(ownerFrame), m_methodHelper(new ibValueMethodHelper())
{
}

#include "backend/system/value/valueMap.h"

ibValueForm::ibValueFormCollectionControl::~ibValueFormCollectionControl()
{
	wxDELETE(m_methodHelper);
}

ibValue ibValueForm::ibValueFormCollectionControl::GetIteratorEmpty()
{
	return ibValue::CreateAndPrepareValueRef<ibValueContainer::ibValueReturnContainer>();
}

ibValue ibValueForm::ibValueFormCollectionControl::GetIteratorAt(unsigned int idx)
{
	if (m_formOwner->m_listControl.size() < idx)
		return ibValue();

	auto structurePos = m_formOwner->m_listControl.begin();
	std::advance(structurePos, idx);

	ibValue controlValue(*structurePos);
	return ibValue::CreateAndPrepareValueRef<ibValueContainer::ibValueReturnContainer>(
		(*structurePos)->GetControlName(),
		controlValue
	);
}

bool ibValueForm::ibValueFormCollectionControl::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	const ibNumber& number = varKeyValue.GetNumber();
	if (m_formOwner->m_listControl.size() < number.ToUInt())
		return false;

	auto it = m_formOwner->m_listControl.begin();
	std::advance(it, number.ToUInt());
	pvarValue = *it;

	return true;
}

bool ibValueForm::ibValueFormCollectionControl::Property(const ibValue& varKeyValue, ibValue& cValueFound)
{
	const wxString& key = varKeyValue.GetString();
	auto it = std::find_if(m_formOwner->m_listControl.begin(), m_formOwner->m_listControl.end(),
		[key](ibValueControl* control) {
			return stringUtils::CompareString(key, control->GetControlName());
		}
	);

	if (it != m_formOwner->m_listControl.end()) {
		cValueFound = *it;
		return true;
	}

	return false;
}

enum
{
	enControlCreate,
	enControlFind,
	enControlRemove,
	enControlProperty,
	enControlCount
};

void ibValueForm::ibValueFormCollectionControl::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc(wxT("CreateControl"), 2, wxT("CreateControl(typeControl : type, parentElement : frame)"));
	m_methodHelper->AppendFunc(wxT("FindControl"), 1, wxT("FindControl(controlName : string)"));
	m_methodHelper->AppendProc(wxT("RemoveControl"), 1, wxT("RemoveControl(controlElement : frame)"));
	m_methodHelper->AppendFunc(wxT("Property"), 2, wxT("Property(key : string, valueFound : frame)"));
	m_methodHelper->AppendFunc(wxT("Count"), wxT("Count()"));

	wxString controlName;

	for (auto control : m_formOwner->m_listControl) {

		if (!control->GetControlNameAsString(controlName))
			continue;

		m_methodHelper->AppendProp(
			controlName,
			true,
			false,
			control->GetControlID()
		);
	}
}

bool ibValueForm::ibValueFormCollectionControl::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	wxASSERT(m_formOwner);
	pvarPropVal = m_formOwner->FindControlByID(
		m_methodHelper->GetPropData(lPropNum)
	);
	return !pvarPropVal.IsEmpty();
}

#include "backend/system/value/valueType.h"

bool ibValueForm::ibValueFormCollectionControl::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enControlRemove:
		m_formOwner->RemoveControl(paParams[0]->ConvertToType<ibValueFrame>());
		return true;
	}
	return false;
}

bool ibValueForm::ibValueFormCollectionControl::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enControlCreate:
		pvarRetValue = m_formOwner->CreateControl(paParams[0]->ConvertToType<ibValueType>(), lSizeArray > 1 ? paParams[1]->ConvertToType<ibValueFrame>() : ibValue());
		return true;
	case enControlFind:
		pvarRetValue = m_formOwner->FindControl(paParams[0]->GetString());
		return true;
	case enControlProperty:
	{	ibValue defaultVal;
		pvarRetValue = Property(*paParams[0], lSizeArray > 1 ? *paParams[1] : defaultVal); }
		return true;
	case enControlCount:
		pvarRetValue = Count();
		return true;
	}

	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueForm::ibValueFormCollectionControl, "FormControl", string_to_clsid("VL_CNTR"));