////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame attributes
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "compiler/methods.h"

//*******************************************************************
//*                         Attributes                              *
//*******************************************************************

enum
{
	eThisForm = 1,
	eControls,
	eDataSources,
	eModified,
	eFormOwner,
	eUniqueKey
};

int CValueForm::FindAttribute(const wxString &sName) const
{
	return m_methods->FindAttribute(sName);
}

void CValueForm::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
	wxString sSynonym = m_methods->GetAttributeSynonym(aParams.GetIndex());

	if (sSynonym == wxT("procUnit")) {
		if (m_procUnit) {
			m_procUnit->SetAttribute(aParams.GetName(), cVal);
		}
	}
	else if (sSynonym == wxT("attribute")) {
		IValueFrame::SetAttribute(aParams, cVal);
	}
	else if (sSynonym == wxT("system")) {
		switch (m_methods->GetAttributePosition(aParams.GetIndex()))
		{
		case eModified: Modify(cVal.GetBoolean()); break;
		}
	}
}

#include "compiler/valueGuid.h"
#include "compiler/valueType.h"

CValue CValueForm::GetAttribute(attributeArg_t &aParams)
{
	wxString sSynonym = m_methods->GetAttributeSynonym(aParams.GetIndex());

	if (sSynonym == wxT("procUnit")) {
		if (m_procUnit) {
			return m_procUnit->GetAttribute(aParams.GetName());
		}
	}
	else if (sSynonym == wxT("attribute")) {
		return IValueFrame::GetAttribute(aParams);
	}
	else if (sSynonym == wxT("system")) {
		switch (m_methods->GetAttributePosition(aParams.GetIndex()))
		{
		case eThisForm: return this;
		case eControls: return m_formControls;
		case eDataSources: return m_formData;
		case eModified: return IsModified(); 
		case eFormOwner: return m_formOwner ? m_formOwner : CValue(); 
		case eUniqueKey: return new CValueGuid(m_formGuid); 
		}
	}

	return CValue();
}