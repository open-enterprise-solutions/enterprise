////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : module manager - methods
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"
#include "compiler/methods.h"
#include "appData.h"

void IModuleManager::PrepareNames() const
{
	std::vector<SEng> aMethods, aAttributes;

	if (m_procUnit != NULL)
	{
		CByteCode *m_byteCode = m_procUnit->GetByteCode();

		for (auto exportFunction : m_byteCode->m_aExportFuncList)
		{
			SEng methods;
			methods.sName = exportFunction.first;
			methods.sSynonym = wxT("procUnit");
			methods.iName = exportFunction.second;
			aMethods.push_back(methods);
		}

		for (auto exportVariable : m_byteCode->m_aExportVarList)
		{
			SEng attributes;
			attributes.sName = exportVariable.first;
			attributes.sSynonym = wxT("procUnit");
			attributes.iName = exportVariable.second;
			aAttributes.push_back(attributes);
		}
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
	m_methods->PrepareMethods(aMethods.data(), aMethods.size());
}

CValue IModuleManager::Method(methodArg_t &aParams)
{
	return IModuleInfo::ExecuteMethod(aParams);
}

void IModuleManager::SetAttribute(attributeArg_t &aParams, CValue &cVal)        //установка атрибута
{
	if (m_procUnit != NULL)
		m_procUnit->SetAttribute(aParams, cVal);
}

CValue IModuleManager::GetAttribute(attributeArg_t &aParams)                   //значение атрибута
{
	if (m_procUnit != NULL)
		return m_procUnit->GetAttribute(aParams);
	return CValue();
}

int IModuleManager::FindAttribute(const wxString &sName) const
{
	if (m_procUnit != NULL) {
		return m_procUnit->FindAttribute(sName);
	}

	return CValue::FindAttribute(sName);
}

//****************************************************************************
//*                      CExternalDataProcessorModuleManager                 *
//****************************************************************************

void CExternalDataProcessorModuleManager::PrepareNames() const
{
	std::vector<SEng> aMethods, aAttributes;

	if (m_objectValue) {
		CMethods *dataMethods = m_objectValue->GetPMethods();
		wxASSERT(dataMethods);
		for (unsigned int idx = 0; idx < dataMethods->GetNMethods(); idx++) {
			aMethods.emplace_back(dataMethods->GetMethodName(idx),
				dataMethods->GetMethodDescription(idx),
				dataMethods->GetMethodSynonym(idx),
				dataMethods->GetMethodPosition(idx));
		}
		for (unsigned int idx = 0; idx < dataMethods->GetNAttributes(); idx++) {
			aAttributes.emplace_back(dataMethods->GetAttributeName(idx),
				wxEmptyString,
				dataMethods->GetAttributeSynonym(idx),
				dataMethods->GetAttributePosition(idx));
		}
	}

	if (m_procUnit != NULL) {
		CByteCode *m_byteCode = m_procUnit->GetByteCode();
		for (auto exportFunction : m_byteCode->m_aExportFuncList)
		{
			SEng methods;
			methods.sName = exportFunction.first;
			methods.sSynonym = wxT("procUnit");
			methods.iName = exportFunction.second;
			aMethods.push_back(methods);
		}
		for (auto exportVariable : m_byteCode->m_aExportVarList)
		{
			SEng attributes;
			attributes.sName = exportVariable.first;
			attributes.sSynonym = wxT("procUnit");
			attributes.iName = exportVariable.second;
			aAttributes.push_back(attributes);
		}
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
	m_methods->PrepareMethods(aMethods.data(), aMethods.size());
}

CValue CExternalDataProcessorModuleManager::Method(methodArg_t &aParams)
{
	if (m_objectValue &&
		m_objectValue->FindMethod(aParams.GetName()) != wxNOT_FOUND) {
		return m_objectValue->Method(aParams);
	}

	return IModuleInfo::ExecuteMethod(aParams);
}

void CExternalDataProcessorModuleManager::SetAttribute(attributeArg_t &aParams, CValue &cVal)        //установка атрибута
{
	if (m_objectValue &&
		m_objectValue->FindAttribute(aParams.GetName()) != wxNOT_FOUND) {
		m_objectValue->SetAttribute(aParams, cVal);
	}

	if (m_procUnit != NULL) {
		m_procUnit->SetAttribute(aParams, cVal);
	}
}

CValue CExternalDataProcessorModuleManager::GetAttribute(attributeArg_t &aParams)                   //значение атрибута
{
	if (m_objectValue &&
		m_objectValue->FindAttribute(aParams.GetName()) != wxNOT_FOUND) {
		return m_objectValue->GetAttribute(aParams);
	}

	if (m_procUnit != NULL) {
		return m_procUnit->GetAttribute(aParams);
	}

	return CValue();
}

int CExternalDataProcessorModuleManager::FindAttribute(const wxString &sName) const
{
	if (m_objectValue &&
		m_objectValue->FindAttribute(sName) != wxNOT_FOUND) {
		return m_objectValue->FindAttribute(sName);
	}

	if (m_procUnit != NULL) {
		return m_procUnit->FindAttribute(sName);
	}

	return CValue::FindAttribute(sName);
}

//****************************************************************************
//*                      CExternalReportModuleManager		                 *
//****************************************************************************

void CExternalReportModuleManager::PrepareNames() const
{
	std::vector<SEng> aMethods, aAttributes;

	if (m_objectValue)
	{
		CMethods *dataMethods = m_objectValue->GetPMethods();
		wxASSERT(dataMethods);

		for (unsigned int idx = 0; idx < dataMethods->GetNMethods(); idx++) {
			aMethods.emplace_back(dataMethods->GetMethodName(idx),
				dataMethods->GetMethodDescription(idx),
				dataMethods->GetMethodSynonym(idx),
				dataMethods->GetMethodPosition(idx));
		}

		for (unsigned int idx = 0; idx < dataMethods->GetNAttributes(); idx++) {
			aAttributes.emplace_back(dataMethods->GetAttributeName(idx),
				wxEmptyString,
				dataMethods->GetAttributeSynonym(idx),
				dataMethods->GetAttributePosition(idx));
		}
	}

	if (m_procUnit != NULL)
	{
		CByteCode *m_byteCode = m_procUnit->GetByteCode();

		for (auto exportFunction : m_byteCode->m_aExportFuncList)
		{
			SEng methods;
			methods.sName = exportFunction.first;
			methods.sSynonym = wxT("procUnit");
			methods.iName = exportFunction.second;
			aMethods.push_back(methods);
		}

		for (auto exportVariable : m_byteCode->m_aExportVarList)
		{
			SEng attributes;
			attributes.sName = exportVariable.first;
			attributes.sSynonym = wxT("procUnit");
			attributes.iName = exportVariable.second;
			aAttributes.push_back(attributes);
		}
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
	m_methods->PrepareMethods(aMethods.data(), aMethods.size());
}

CValue CExternalReportModuleManager::Method(methodArg_t &aParams)
{
	if (m_objectValue &&
		m_objectValue->FindMethod(aParams.GetName()) != wxNOT_FOUND) {
		return m_objectValue->Method(aParams);
	}

	return IModuleInfo::ExecuteMethod(aParams);
}

void CExternalReportModuleManager::SetAttribute(attributeArg_t &aParams, CValue &cVal)        //установка атрибута
{
	if (m_objectValue &&
		m_objectValue->FindAttribute(aParams.GetName()) != wxNOT_FOUND) {
		m_objectValue->SetAttribute(aParams, cVal);
	}

	if (m_procUnit != NULL) {
		m_procUnit->SetAttribute(aParams, cVal);
	}
}

CValue CExternalReportModuleManager::GetAttribute(attributeArg_t &aParams)                   //значение атрибута
{
	if (m_objectValue &&
		m_objectValue->FindAttribute(aParams.GetName()) != wxNOT_FOUND) {
		return m_objectValue->GetAttribute(aParams);
	}

	if (m_procUnit != NULL) {
		return m_procUnit->GetAttribute(aParams);
	}

	return CValue();
}

int CExternalReportModuleManager::FindAttribute(const wxString &sName) const
{
	if (m_objectValue &&
		m_objectValue->FindAttribute(sName) != wxNOT_FOUND) {
		return m_objectValue->FindAttribute(sName);
	}

	if (m_procUnit != NULL) {
		return m_procUnit->FindAttribute(sName);
	}

	return CValue::FindAttribute(sName);
}