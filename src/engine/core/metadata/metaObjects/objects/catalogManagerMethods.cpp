////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog manager - methods
////////////////////////////////////////////////////////////////////////////

#include "catalogManager.h"
#include "compiler/methods.h"

enum
{
	eCreateElement = 0,
	eCreateGroup,

	eSelect,
	eFindByCode,
	eFindByName,

	eGetForm,
	eGetListForm,
	eGetSelectForm,

	eEmptyRef
};

#include "metadata/metadata.h"

void CCatalogManager::PrepareNames() const
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	SEng aMethods[] =
	{
		{"createElement", "createElement()"},
		{"createGroup", "createGroup()"},

		{"select", "select()"},
		{"findByCode", "findByCode(string)"},
		{"findByName", "findByName(string)"},

		{"getForm", "getForm(string, owner, guid)"},
		{"getListForm", "getListForm(string, owner, guid)"},
		{"getSelectForm", "getSelectForm(string, owner, guid)"},

		{"emptyRef", "emptyRef()"},
	};

	int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
	m_methods->PrepareMethods(aMethods, nCountM);

	CValue* pRefData = moduleManager->FindCommonModule(m_metaObject->GetModuleManager());

	if (pRefData) {

		//добавляем методы из контекста
		for (unsigned int idx = 0; idx < pRefData->GetNMethods(); idx++) {
			m_methods->AppendMethod(pRefData->GetMethodName(idx),
				pRefData->GetMethodDescription(idx),
				wxT("commonModule"));
		}
	}
}

#include "selector/objectSelector.h"
#include "frontend/visualView/controls/form.h"

CValue CCatalogManager::Method(methodArg_t& aParams)
{
	CValue ret;

	switch (aParams.GetIndex())
	{
	case eCreateElement: return m_metaObject->CreateObjectValue();
	case eCreateGroup: return m_metaObject->CreateGroupObjectValue();
	case eSelect:
	{
		class CSelectorCatalogObject : public ISelectorDataObject {
		public:
			CSelectorCatalogObject(CMetaObjectCatalog* metaObject) : ISelectorDataObject(metaObject) {}
		};

		return new CSelectorCatalogObject(m_metaObject);
	}
	case eFindByCode: return FindByCode(aParams[0]);
	case eFindByName: return FindByName(aParams[0]);
	case eGetForm:
	{
		CValueGuid* guidVal = aParams.GetParamCount() > 2 ? aParams[2].ConvertToType<CValueGuid>() : NULL;
		return m_metaObject->GetGenericForm(aParams[0].GetString(),
			aParams.GetParamCount() > 1 ? aParams[1].ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
	}
	case eGetListForm:
	{
		CValueGuid* guidVal = aParams.GetParamCount() > 2 ? aParams[2].ConvertToType<CValueGuid>() : NULL;

		return m_metaObject->GetListForm(aParams[0].GetString(),
			aParams.GetParamCount() > 1 ? aParams[1].ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
	}
	case eGetSelectForm:
	{
		CValueGuid* guidVal = aParams.GetParamCount() > 2 ? aParams[2].ConvertToType<CValueGuid>() : NULL;

		return m_metaObject->GetSelectForm(aParams[0].GetString(),
			aParams.GetParamCount() > 1 ? aParams[1].ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
	}
	case eEmptyRef: return EmptyRef();
	}

	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	CValue* pRefData =
		moduleManager->FindCommonModule(m_metaObject->GetModuleManager());

	if (pRefData) {
		return pRefData->Method(aParams);
	}

	return ret;
}