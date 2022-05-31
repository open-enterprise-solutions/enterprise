////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2Ñ-team
//	Description : Processor unit 
////////////////////////////////////////////////////////////////////////////

#include "methods.h"

void CMethods::PrepareConstructors(SEng *pConstructors, unsigned int nCount)
{
	m_aTreeConstructors.clear();

	if (pConstructors) {
		m_aConstructors.resize(nCount);
		for (unsigned int i = 0; i < nCount; i++)
		{
			m_aConstructors[i].sName = pConstructors[i].sName;
			m_aConstructors[i].sShortDescription = pConstructors[i].sShortDescription;
			m_aConstructors[i].sSynonym = pConstructors[i].sSynonym;
			m_aConstructors[i].iName = pConstructors[i].iName;

			wxString sName = pConstructors[i].sName;
			sName.MakeUpper();
			m_aTreeConstructors[sName] = (size_t)(i + 1);
		}
	}
	else {
		m_aConstructors.clear();
	}
}

void CMethods::PrepareMethods(SEng *pMethods, unsigned int nCount)
{
	m_aTreeMethods.clear();

	if (pMethods)
	{
		m_aMethods.resize(nCount);

		for (unsigned int i = 0; i < nCount; i++)
		{
			m_aMethods[i].sName = pMethods[i].sName;
			m_aMethods[i].sShortDescription = pMethods[i].sShortDescription;
			m_aMethods[i].sSynonym = pMethods[i].sSynonym;
			m_aMethods[i].iName = pMethods[i].iName;

			wxString sName = pMethods[i].sName;
			sName.MakeUpper();
			m_aTreeMethods[sName] = (size_t)(i + 1);
		}
	}
	else
	{
		m_aMethods.clear();
	}
}

void CMethods::PrepareAttributes(SEng *pAttributes, unsigned int nCount)
{
	m_aTreeAttributes.clear();

	if (pAttributes) {
		m_aAttributes.resize(nCount);
		for (unsigned int i = 0; i < nCount; i++) {
			m_aAttributes[i].sName = pAttributes[i].sName;
			m_aAttributes[i].sSynonym = pAttributes[i].sSynonym;
			m_aAttributes[i].iName = pAttributes[i].iName;

			wxString sName = pAttributes[i].sName;
			sName.MakeUpper();
			m_aTreeAttributes[sName] = (size_t)(i + 1);
		}
	}
	else{
		m_aAttributes.clear();
	}
}

int CMethods::FindConstructor(const wxString &sName) const
{
	auto itFounded = m_aTreeConstructors.find(sName.Upper());
	if (itFounded != m_aTreeConstructors.end()) return (int)itFounded->second - 1;
	return wxNOT_FOUND;
}

int CMethods::FindMethod(const wxString &sName) const
{
	auto itFounded = m_aTreeMethods.find(sName.Upper());
	if (itFounded != m_aTreeMethods.end()) return (int)itFounded->second - 1;
	return wxNOT_FOUND;
}

int CMethods::FindAttribute(const wxString &sName) const
{
	auto itFounded = m_aTreeAttributes.find(sName.Upper());
	if (itFounded != m_aTreeAttributes.end()) return (int)itFounded->second - 1;
	return wxNOT_FOUND;
}

int CMethods::AppendConstructor(const wxString &constructorName, const wxString &shortDescription)
{
	return AppendConstructor(constructorName, shortDescription, wxT("constructor"), wxNOT_FOUND);
}

int CMethods::AppendConstructor(const wxString &constructorName, const wxString &shortDescription, const wxString &synonym)
{
	return AppendConstructor(constructorName, shortDescription, synonym, wxNOT_FOUND);
}

int CMethods::AppendConstructor(const wxString &constructorName, const wxString &shortDescription, const wxString &synonym, int reaName)
{
	auto itFounded = m_aTreeConstructors.find(constructorName.Upper());

	if (itFounded == m_aTreeConstructors.end()) {
		m_aConstructors.emplace_back(constructorName, shortDescription, synonym, reaName);
		m_aTreeConstructors[constructorName.Upper()] = m_aConstructors.size();
		return m_aConstructors.size();
	}

	if (itFounded != m_aTreeConstructors.end()) return itFounded->second - 1;

	return wxNOT_FOUND;
}

int CMethods::AppendMethod(const wxString &methodName, const wxString &shortDescription)
{
	return AppendMethod(methodName, shortDescription, wxT("method"), wxNOT_FOUND);
}

int CMethods::AppendMethod(const wxString &methodName, const wxString &shortDescription, const wxString &synonym)
{
	return AppendMethod(methodName, shortDescription, synonym, wxNOT_FOUND);
}

int CMethods::AppendMethod(const wxString &methodName, const wxString &shortDescription, const wxString &synonym, int reaName)
{
	auto itFounded = m_aTreeMethods.find(methodName.Upper());

	if (itFounded == m_aTreeMethods.end()) {
		m_aMethods.emplace_back(methodName, shortDescription, synonym, reaName);
		m_aTreeMethods[methodName.Upper()] = m_aMethods.size();
		return m_aMethods.size();
	}

	if (itFounded != m_aTreeMethods.end()) {
		return itFounded->second - 1;
	}

	return wxNOT_FOUND;
}

int CMethods::AppendAttribute(const wxString &attributeName)
{
	return AppendAttribute(attributeName, wxT("attribute"), wxNOT_FOUND);
}

int CMethods::AppendAttribute(const wxString &attributeName, const wxString &synonym)
{
	return AppendAttribute(attributeName, synonym, wxNOT_FOUND);
}

int CMethods::AppendAttribute(const wxString &attributeName, const wxString &synonym, int realName)
{
	auto itFounded = m_aTreeAttributes.find(attributeName.Upper());

	if (itFounded == m_aTreeAttributes.end()) {
		m_aAttributes.emplace_back(attributeName, wxEmptyString, synonym, realName);
		m_aTreeAttributes[attributeName.Upper()] = (size_t)m_aAttributes.size();

		return m_aAttributes.size();
	}

	if (itFounded != m_aTreeAttributes.end()) 
		return itFounded->second - 1;

	return wxNOT_FOUND;
}

void CMethods::RemoveConstructor(const wxString &constructorName)
{
	auto itFounded = m_aTreeConstructors.find(constructorName.Upper());
	if (itFounded != m_aTreeConstructors.end()) { 
		m_aTreeConstructors.erase(itFounded); 
	}
}

void CMethods::RemoveMethod(const wxString & methodName)
{
	auto itFounded = m_aTreeMethods.find(methodName.Upper());
	if (itFounded != m_aTreeMethods.end()) {
		m_aTreeMethods.erase(itFounded); 
	}
}

void CMethods::RemoveAttribute(const wxString &attributeName)
{
	auto itFounded = m_aTreeAttributes.find(attributeName.Upper());
	if (itFounded != m_aTreeAttributes.end()) { 
		m_aTreeAttributes.erase(itFounded); 
	}
}

wxString CMethods::GetConstructorName(unsigned int nName) const
{
	if (nName < GetNConstructors())
		return m_aConstructors[nName].sName;
	return wxEmptyString;
}


wxString CMethods::GetMethodName(unsigned int nName) const
{
	if (nName < GetNMethods()) 
		return m_aMethods[nName].sName;
	return wxEmptyString;
}

wxString CMethods::GetAttributeName(unsigned int nName) const
{
	if (nName < GetNAttributes()) 
		return m_aAttributes[nName].sName;
	return wxEmptyString;
}

wxString CMethods::GetConstructorDescription(unsigned int nName) const
{
	if (nName < GetNConstructors())
		return m_aConstructors[nName].sShortDescription;
	return wxEmptyString;
}

wxString CMethods::GetMethodDescription(unsigned int nName) const
{
	if (nName < GetNMethods())
		return m_aMethods[nName].sShortDescription;
	return wxEmptyString;
}

wxString CMethods::GetConstructorSynonym(unsigned int nName) const
{
	if (nName < GetNConstructors())
		return m_aConstructors[nName].sSynonym;
	return wxEmptyString;
}

wxString CMethods::GetMethodSynonym(unsigned int nName) const
{
	if (nName < GetNMethods()) 
		return m_aMethods[nName].sSynonym;
	return wxEmptyString;
}

wxString CMethods::GetAttributeSynonym(unsigned int nName) const
{
	if (nName < GetNAttributes())  
		return m_aAttributes[nName].sSynonym;
	return wxEmptyString;
}

int CMethods::GetConstructorPosition(unsigned int nName) const
{
	if (nName < GetNConstructors())
		return m_aConstructors[nName].iName;
	return wxNOT_FOUND;
}

int CMethods::GetMethodPosition(unsigned int nName) const
{
	if (nName < GetNMethods()) 
		return m_aMethods[nName].iName;
	return wxNOT_FOUND;
}

int CMethods::GetAttributePosition(unsigned int nName) const
{
	if (nName < GetNAttributes()) 
		return m_aAttributes[nName].iName;
	return wxNOT_FOUND;
}