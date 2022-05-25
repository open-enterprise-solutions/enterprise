#ifndef _INFO_REGISTER_MANAGER_H__
#define _INFO_REGISTER_MANAGER_H__

#include "informationRegister.h"
#include "common/managerInfo.h"

class CInformationRegisterManager : public CValue,
	public IMetaManagerInfo {
	wxDECLARE_DYNAMIC_CLASS(CInformationRegisterManager);
public:

	CValue Get(const CValue& cFilter = CValue());
	CValue Get(const CValue& cPeriod, const CValue& cFilter);

	CValue GetFirst(const CValue& cPeriod, const CValue& cFilter = CValue());
	CValue GetLast(const CValue& cPeriod, const CValue& cFilter = CValue());

	CValue SliceFirst(const CValue& cPeriod, const CValue& cFilter = CValue());
	CValue SliceLast(const CValue& cPeriod, const CValue& cFilter = CValue());

	CInformationRegisterManager(CMetaObjectInformationRegister* metaObject = NULL);
	virtual ~CInformationRegisterManager();

	virtual CMetaCommonModuleObject* GetModuleManager() const;

	virtual CMethods* GetPMethods() const {  //получить ссылку на класс помощник разбора имен атрибутов и методов
		PrepareNames();
		return m_methods;
	}
	virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams); //вызов метода

	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t& aParams);                   //значение атрибута

	//Get ref class 
	virtual CLASS_ID GetClassType() const;

	//types 
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:
	//methods 
	CMethods* m_methods;
	CMetaObjectInformationRegister* m_metaObject;
};

#endif 