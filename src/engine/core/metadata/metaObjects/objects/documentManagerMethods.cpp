////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document manager - methods
////////////////////////////////////////////////////////////////////////////

#include "documentManager.h"
#include "compiler/methods.h"

enum
{
	eCreateElement = 0,

	eSelect,
	eFindByNumber,

	eGetForm,
	eGetListForm,
	eGetSelectForm,

	eEmptyRef
};

#include "metadata/metadata.h"

void CDocumentManager::PrepareNames() const
{
	IMetadata *metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager *moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	SEng aMethods[] =
	{
		{"createElement", "createElement()"},

		{"select", "select()"},

		{"findByNumber", "findByNumber(string, date)"},

		{"getForm", "getForm(string, owner, guid)"},
		{"getListForm", "getListForm(string, owner, guid)"},
		{"getSelectForm", "getSelectForm(string, owner, guid)"},

		{"emptyRef", "emptyRef()"},
	};

	int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
	m_methods->PrepareMethods(aMethods, nCountM);

	CValue *pRefData = moduleManager->FindCommonModule(m_metaObject->GetModuleManager());

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

CValue CDocumentManager::Method(methodArg_t &aParams)
{
	CValue ret;

	switch (aParams.GetIndex())
	{
	case eCreateElement:
		return m_metaObject->CreateObjectValue();
	case eSelect:
	{
		class CSelectorDocumentObject : public ISelectorDataObject {
		public:
			CSelectorDocumentObject(CMetaObjectDocument *metaObject) : ISelectorDataObject(metaObject) {}
		};

		return new CSelectorDocumentObject(m_metaObject);
	}
	case eFindByNumber:
	{
		return FindByNumber(aParams[0], 
			aParams.GetParamCount() > 1 ? aParams[1] : CValue()
		);
	}
	case eGetForm:
	{
		CValueGuid *guidVal = aParams.GetParamCount() > 2 ? aParams[2].ConvertToType<CValueGuid>() : NULL;
		return m_metaObject->GetGenericForm(aParams[0].GetString(),
			aParams.GetParamCount() > 1 ? aParams[1].ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
	}
	case eGetListForm:
	{
		CValueGuid *guidVal = aParams.GetParamCount() > 2 ? aParams[2].ConvertToType<CValueGuid>() : NULL;
		return m_metaObject->GetListForm(aParams[0].GetString(),
			aParams.GetParamCount() > 1 ? aParams[1].ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
	}
	case eGetSelectForm:
	{
		CValueGuid *guidVal = aParams.GetParamCount() > 2 ? aParams[2].ConvertToType<CValueGuid>() : NULL;
		return m_metaObject->GetSelectForm(aParams[0].GetString(),
			aParams.GetParamCount() > 1 ? aParams[1].ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
	}
	case eEmptyRef:
		return EmptyRef();
	}

	IMetadata *metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager *moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	CValue *pRefData = moduleManager->FindCommonModule(m_metaObject->GetModuleManager());

	if (pRefData) {
		return pRefData->Method(aParams);
	}

	return ret;
}