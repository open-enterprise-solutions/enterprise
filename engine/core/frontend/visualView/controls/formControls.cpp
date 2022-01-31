////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : form - controls 
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "compiler/methods.h"
#include "utils/stringUtils.h"

//////////////////////////////////////////////////////////////////////
wxIMPLEMENT_DYNAMIC_CLASS(CValueForm::CValueFormControl, CValue);
//////////////////////////////////////////////////////////////////////

CValueForm::CValueFormControl::CValueFormControl() : CValue(eValueTypes::TYPE_VALUE, true),
m_formOwner(NULL), m_methods(NULL)
{
}

CValueForm::CValueFormControl::CValueFormControl(CValueForm *ownerFrame) : CValue(eValueTypes::TYPE_VALUE, true),
m_formOwner(ownerFrame), m_methods(new CMethods())
{
}

#include "compiler/valueMap.h"

CValueForm::CValueFormControl::~CValueFormControl()
{
	wxDELETE(m_methods);
}

CValue CValueForm::CValueFormControl::GetItEmpty()
{
	return new CValueContainer::CValueReturnContainer();
}

CValue CValueForm::CValueFormControl::GetItAt(unsigned int idx)
{
	if (m_formOwner->m_aControls.size() < idx) {
		return CValue();
	}

	auto structurePos = m_formOwner->m_aControls.begin();
	std::advance(structurePos, idx);

	wxString propertyName; 
	if (!(*structurePos)->GetPropertyValue("name", propertyName)) {
		return CValue();
	}

	return new CValueContainer::CValueReturnContainer(propertyName, CValue(*structurePos));
}

#include "appData.h"

CValue CValueForm::CValueFormControl::GetAt(const CValue &cKey)
{
	number_t number = cKey.GetNumber();

	if (m_formOwner->m_aControls.size() < number.ToUInt())
		return CValue();

	auto structurePos = m_formOwner->m_aControls.begin();
	std::advance(structurePos, number.ToUInt());
	return *structurePos;

	if (!appData->DesignerMode())
		CTranslateError::Error(_("Index goes beyond array"));

	return CValue();
}

bool CValueForm::CValueFormControl::Property(const CValue &cKey, CValue &cValueFound)
{
	wxString key = cKey.GetString(); 
	auto itFounded = std::find_if(m_formOwner->m_aControls.begin(), m_formOwner->m_aControls.end(), [key](IValueControl *control) { return StringUtils::CompareString(key, control->GetPropertyAsString("name")); });
	if (itFounded != m_formOwner->m_aControls.end()) { cValueFound = *itFounded; return true; }
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

void CValueForm::CValueFormControl::PrepareNames() const
{
	std::vector<SEng> aMethods = {
	{"createControl", "createControl(typeControl, parentElement)"},
	{"findControl", "findControl(controlName)"},
	{"removeControl", "removeControl(controlElement)"},
	{"property", "property(key, valueFound)"}, 
	{"count", "count()"}
	};

	m_methods->PrepareMethods(aMethods.data(), aMethods.size());

	std::vector<SEng> aAttributes;
	for (auto control : m_formOwner->m_aControls) {
		aAttributes.push_back({ control->GetPropertyAsString("name") });
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

CValue CValueForm::CValueFormControl::GetAttribute(attributeArg_t &aParams)
{
	if (m_formOwner->m_aControls.size() < (unsigned int)aParams.GetIndex())
		return CValue();

	auto structurePos = m_formOwner->m_aControls.begin();
	std::advance(structurePos, aParams.GetIndex());
	return *structurePos;
}

#include "compiler/valueType.h"

CValue CValueForm::CValueFormControl::Method(methodArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case enControlCreate:
		return m_formOwner->CreateControl(aParams[0].ConvertToType<CValueType>(), aParams.GetParamCount() > 1 ? aParams[1].ConvertToType<IValueFrame>() : CValue());
	case enControlFind:
		return m_formOwner->FindControl(aParams[0].GetString());
	case enControlRemove:
		m_formOwner->RemoveControl(aParams[0].ConvertToType<IValueFrame>()); break;
	case enControlProperty:
		return Property(aParams[0], aParams.GetParamCount() > 1 ? aParams[1] : CValue());
	case enControlCount:
		return Count();
	}

	return CValue();
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SO_VALUE_REGISTER(CValueForm::CValueFormControl, "formControl", CValueFormControl, TEXT2CLSID("VL_CNTR"));
