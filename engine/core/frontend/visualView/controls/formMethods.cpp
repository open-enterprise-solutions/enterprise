////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame methods
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "compiler/methods.h"
#include "appData.h"

enum
{
	enShow = 0,
	enActivate,
	enUpdate,
	enClose,

	enIsShown,

	enAttachIdleHandler,
	enDetachIdleHandler, 

	enNotifyChoice, 
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void CValueForm::PrepareNames() const
{
	std::vector<SEng> aMethods, aAttributes;

	aMethods = {
	{"show", "show()"},
	{"activate", "activate()"},
	{"update", "update()"},
	{"close", "close()"},

	{"isShown", "isShown()"},

	{"attachIdleHandler", "attachIdleHandler(procedureName, interval, single)"}, 
	{"detachIdleHandler", "detachIdleHandler(procedureName)"},

	{"notifyChoice", "notifyChoice(value)"}, 
	};

	//default element
	SEng attributesThisForm;
	attributesThisForm.sName = thisForm;
	attributesThisForm.iName = 1;
	attributesThisForm.sSynonym = wxT("system");
	aAttributes.push_back(attributesThisForm);

	SEng attributesControls;
	attributesControls.sName = wxT("controls");
	attributesControls.iName = 2;
	attributesControls.sSynonym = wxT("system");
	aAttributes.push_back(attributesControls);

	SEng attributesDataSources;
	attributesDataSources.sName = wxT("dataSources");
	attributesDataSources.iName = 3;
	attributesDataSources.sSynonym = wxT("system");
	aAttributes.push_back(attributesDataSources);

	SEng attributesModified;
	attributesModified.sName = wxT("modified");
	attributesModified.iName = 4;
	attributesModified.sSynonym = wxT("system");
	aAttributes.push_back(attributesModified);

	SEng attributesFormOwner;
	attributesFormOwner.sName = wxT("formOwner");
	attributesFormOwner.iName = 5;
	attributesFormOwner.sSynonym = wxT("system");
	aAttributes.push_back(attributesFormOwner);

	SEng attributesUniqueKey;
	attributesUniqueKey.sName = wxT("uniqueKey");
	attributesUniqueKey.iName = 6;
	attributesUniqueKey.sSynonym = wxT("system");
	aAttributes.push_back(attributesUniqueKey);

	//from property 
	for (unsigned int idx = 0; idx < IObjectBase::GetPropertyCount(); idx++) {
	    Property *property = IObjectBase::GetProperty(idx); 
		if (!property)
			continue;
		SEng attributes;
		attributes.sName = property->GetName();
		attributes.iName = idx;
		attributes.sSynonym = wxT("attribute");
		aAttributes.push_back(attributes);
	}

	if (m_procUnit != NULL) {
		CByteCode *m_byteCode = m_procUnit->GetByteCode();
		for (auto exportFunction : m_byteCode->m_aExportFuncList) {
			SEng methods;
			methods.sName = exportFunction.first;
			methods.sSynonym = wxT("procUnit");
			methods.iName = exportFunction.second;
			aMethods.push_back(methods);
		}
		for (auto exportVariable : m_byteCode->m_aExportVarList) {
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

CValue CValueForm::Method(methodArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case enShow: ShowForm(); break;
	case enActivate: ActivateForm(); break;
	case enUpdate:  UpdateForm(); break;
	case enClose: return CloseForm();
	case enIsShown:  return IsShown();

	case enAttachIdleHandler: AttachIdleHandler(aParams[0].GetString(), aParams[1].ToInt(), aParams[2].GetBoolean()); break;
	case enDetachIdleHandler: DetachIdleHandler(aParams[0].GetString()); break; 

	case enNotifyChoice: NotifyChoice(aParams[0]); break; 
	}

	return IModuleInfo::ExecuteMethod(aParams);
}