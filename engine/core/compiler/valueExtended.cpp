////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : extended module
////////////////////////////////////////////////////////////////////////////

#include "value.h"
#include "appData.h"
#include "databaseLayer/databaseLayer.h"
#include "translateModule.h"

#include "definition.h"

//*************************************************************************************************************************************
//*                                                           CValueNoRet                                                             *
//*************************************************************************************************************************************

void CValue::CheckValue() { if (m_typeClass == eValueTypes::TYPE_REFFER && m_pRef) m_pRef->CheckValue(); }

void CValueNoRet::CheckValue() { CTranslateError::Error(ERROR_USE_PROCEDURE_AS_FUNCTION, wxEmptyString, sProcedure.wc_str()); }

//*************************************************************************************************************************************
//*                                                         attributeArg_t                                                      *
//*************************************************************************************************************************************

attributeArg_t::attributeArg_t(int index, const wxString& name) : m_index(index), m_name(name) {}

//*************************************************************************************************************************************
//*                                                         methodArg_t                                                         *
//*************************************************************************************************************************************

void methodArg_t::CheckValue(unsigned int idx) { if (!appData->DesignerMode()) { if (m_varCount < idx + 1) CTranslateError::Error(ERROR_FEW_PARAMS, m_name.wc_str()); } }
methodArg_t::methodArg_t(CValue** params, unsigned int varCount, int iName, const wxString& sName) : m_aParams(params), m_varCount(varCount), m_index(iName), m_name(sName) {}
void methodArg_t::CheckParams() { if (!appData->DesignerMode()) { if (m_setParams.size() < m_varCount) CTranslateError::Error(ERROR_MANY_PARAMS, m_name.wc_str()); } }
CValue* methodArg_t::GetAt(unsigned int idx) { CheckValue(idx); if (m_varCount < idx + 1) { return NULL; } m_setParams.insert(idx); return m_aParams[idx]; }
CValue methodArg_t::operator[](unsigned int idx) { CheckValue(idx); if (m_varCount < idx + 1) { return eValueTypes::TYPE_EMPTY; } m_setParams.insert(idx); return *m_aParams[idx]; }

//*************************************************************************************************************************************
//*                                                         ValueRefData                                                              *
//*************************************************************************************************************************************

void CValue::DecrRef()
{
	wxASSERT_MSG(m_refCount > 0, "invalid ref data count");

	if (--m_refCount == 0)
		delete this;
}