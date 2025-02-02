#ifndef _MODULE_MANAGER_H__
#define _MODULE_MANAGER_H__

#include "backend/wrapper/moduleInfo.h"

#include "backend/metaCollection/metaObjectMetadata.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metaCollection/partial/dataProcessor.h"
#include "backend/metaCollection/partial/dataReport.h"

class BACKEND_API IModuleManager : public CValue,
	public IModuleInfo {
protected:
	enum helperAlias {
		eProcUnit
	};
public:
	
	class BACKEND_API CModuleUnit : public CValue,
		public IModuleInfo {
		wxDECLARE_DYNAMIC_CLASS(CModuleUnit);
	protected:
		enum helperAlias {
			eProcUnit
		};
	private:
		CMethodHelper* m_methodHelper;
	public:

		CModuleUnit() {}
		CModuleUnit(IModuleManager* moduleManager, CMetaObjectModule* moduleObject, bool managerModule = false);
		virtual ~CModuleUnit();

		//initalize common module
		bool CreateCommonModule();
		bool DestroyCommonModule();

		//get common module 
		CMetaObjectModule* GetModuleObject() const {
			return m_moduleObject;
		}

		//Is global module?
		wxString GetModuleFullName() const {
			return m_moduleObject ? m_moduleObject->GetFullName() : wxEmptyString;
		}

		wxString GetModuleDocPath() const {
			return m_moduleObject ? m_moduleObject->GetDocPath() : wxEmptyString;
		}

		wxString GetModuleName() const {
			return m_moduleObject ? m_moduleObject->GetName() : wxEmptyString;
		}

		wxString GetModuleText() const {
			return m_moduleObject ? m_moduleObject->GetModuleText() : wxEmptyString;
		}

		bool IsGlobalModule() const {
			return m_moduleObject ? m_moduleObject->IsGlobalModule() : false;
		}

		//РАБОТА КАК АГРЕГАТНОГО ОБЪЕКТА

		//эти методы нужно переопределить в ваших агрегатных объектах:
		virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
			//PrepareNames();
			return m_methodHelper;
		}

		//этот метод автоматически вызывается для инициализации имен атрибутов и методов
		virtual void PrepareNames() const;

		//вызов метода
		virtual bool CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray) override;
		virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray) override;

		virtual wxString GetString() const override {
			return m_moduleObject->GetName();
		}

		//check is empty
		virtual inline bool IsEmpty() const {
			return false;
		}

		//operator '=='
		virtual inline bool CompareValueEQ(const CValue& cParam) const override
		{
			CModuleUnit* compareModule = dynamic_cast<CModuleUnit*>(cParam.GetRef());
			if (compareModule) {
				return m_moduleObject == compareModule->GetModuleObject();
			}

			return false;
		}

		//operator '!='
		virtual inline bool CompareValueNE(const CValue& cParam) const override
		{
			CModuleUnit* compareModule = dynamic_cast<CModuleUnit*>(cParam.GetRef());
			if (compareModule) {
				return m_moduleObject != compareModule->GetModuleObject();
			}

			return false;
		}

	protected:
		IModuleManager* m_moduleManager;
		CMetaObjectModule* m_moduleObject;
	};

	class BACKEND_API CMetadataUnit : public CValue {
		wxDECLARE_DYNAMIC_CLASS(CMetadataUnit);
	private:
		CMethodHelper* m_methodHelper;
	public:

		CMetadataUnit() {}
		CMetadataUnit(IMetaData* metaData);
		virtual ~CMetadataUnit();

		//get common module 
		IMetaData* GetMetaData() const { 
			return m_metaData;
		}

		//check is empty
		virtual inline bool IsEmpty() const {
			return false;
		}

		//operator '=='
		virtual inline bool CompareValueEQ(const CValue& cParam) const override
		{
			CMetadataUnit* compareMetadata = dynamic_cast<CMetadataUnit*>(cParam.GetRef());
			if (compareMetadata) {
				return m_metaData == compareMetadata->GetMetaData();
			}

			return false;
		}

		//operator '!='
		virtual inline bool CompareValueNE(const CValue& cParam) const override {
			CMetadataUnit* compareMetadata = dynamic_cast<CMetadataUnit*>(cParam.GetRef());
			if (compareMetadata) {
				return m_metaData != compareMetadata->GetMetaData();
			}

			return false;
		}

		//эти методы нужно переопределить в ваших агрегатных объектах:
		virtual CMethodHelper* GetPMethods() const { 
			//PrepareNames();  
			return m_methodHelper; }
		
		virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов

		//****************************************************************************
		//*                              Override attribute                          *
		//****************************************************************************

		virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal) override;        //установка атрибута
		virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal) override;                   //значение атрибута

	protected:
		IMetaData* m_metaData;
	};

private:

	void Clear();

protected:

	//metaData and external variant
	IModuleManager(IMetaData* metaData, CMetaObjectModule* metaObject);

public:

	virtual ~IModuleManager();

	//Create common module
	virtual bool CreateMainModule() = 0;

	//destroy common module
	virtual bool DestroyMainModule() = 0;

	//start common module
	virtual bool StartMainModule(bool force = false) = 0;

	//exit common module
	virtual bool ExitMainModule(bool force = false) = 0;

	//эти методы нужно переопределить в ваших агрегатных объектах:
	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		//PrepareNames(); 
		return m_methodHelper; 
	}
	//этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual void PrepareNames() const;
	//вызов метода
	virtual bool CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray);
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута
	virtual long FindProp(const wxString& strName) const;

	//check is empty
	virtual inline bool IsEmpty() const {
		return false;
	}

	//compile modules:
	bool AddCompileModule(IMetaObject* moduleObject, CValue* object);
	bool RemoveCompileModule(IMetaObject* moduleObject);

	//templates:
	template <class T> inline bool FindCompileModule(IMetaObject* moduleObject, T*& objValue) const {
		auto& it = m_listCommonModuleValue.find(moduleObject);
		if (it != m_listCommonModuleValue.end()) {
			objValue = dynamic_cast<T*>(it->second);
			return objValue != nullptr;
		}
		objValue = nullptr;
		return false;
	}

	template <class T> inline bool FindParentCompileModule(IMetaObject* moduleObject, T*& objValue) const {
		IMetaObject* parentMetadata = moduleObject ? moduleObject->GetParent() : nullptr;
		if (parentMetadata != nullptr)
			return FindCompileModule(parentMetadata, objValue);
		return false;
	}

	//common modules:
	bool AddCommonModule(CMetaObjectCommonModule* commonModule, bool managerModule = false, bool runModule = false);

	CModuleUnit* FindCommonModule(CMetaObjectCommonModule* commonModule) const;
	bool RenameCommonModule(CMetaObjectCommonModule* commonModule, const wxString& newName);
	bool RemoveCommonModule(CMetaObjectCommonModule* commonModule);

	//system object:
	CValue* GetObjectManager() const {
		return m_objectManager;
	}

	CMetadataUnit* GetMetaManager() const {
		return m_metaManager;
	}

	virtual std::vector<CModuleUnit*>& GetCommonModules() {
		return m_listCommonModuleManager;
	}

	//associated map
	virtual std::map<wxString, CValue*>& GetGlobalVariables() {
		return m_listGlConstValue;
	}

	virtual std::map<wxString, CValue*>& GetContextVariables() {
		return m_compileModule->m_aContextValues;
	}

	//return external module
	virtual IRecordDataObjectExt* GetObjectValue() const {
		return nullptr;
	}

protected:

	bool m_initialized;

	//global manager
	CValue* m_objectManager;
	// global metamanager
	CMetadataUnit* m_metaManager;

	//map with compile data
	std::map<IMetaObject*, CValue*> m_listCommonModuleValue;
	//array of common modules
	std::vector<CModuleUnit*> m_listCommonModuleManager;
	//array of global variables
	std::map<wxString, CValue*> m_listGlConstValue;

	friend class CMetaDataConfiguration;
	friend class CMetaDataDataProcessor;

	friend class CModuleUnit;

	CMethodHelper* m_methodHelper;
};

class BACKEND_API CModuleManagerConfiguration : public IModuleManager {
	//system events:
	bool BeforeStart();
	void OnStart();
	bool BeforeExit();
	void OnExit();
public:

	//metaData and external variant
	CModuleManagerConfiguration(IMetaData* metaData = nullptr, CMetaObject* metaObject = nullptr);

	//Create common module
	virtual bool CreateMainModule();

	//destroy common module
	virtual bool DestroyMainModule();

	//start common module
	virtual bool StartMainModule(bool force = false);

	//exit common module
	virtual bool ExitMainModule(bool force = false);
};

class BACKEND_API CModuleManagerExternalDataProcessor : public IModuleManager {
	CRecordDataObjectDataProcessor* m_objectValue;
public:

	virtual CCompileModule* GetCompileModule() const;
	virtual CProcUnit* GetProcUnit() const;

	virtual std::map<wxString, CValue*>& GetContextVariables();

	//metaData and external variant
	CModuleManagerExternalDataProcessor(IMetaData* metaData = nullptr, CMetaObjectDataProcessor* metaObject = nullptr);
	virtual ~CModuleManagerExternalDataProcessor();

	//return external module
	virtual IRecordDataObjectExt* GetObjectValue() const {
		return m_objectValue;
	}

	//Create common module
	virtual bool CreateMainModule();

	//destroy common module
	virtual bool DestroyMainModule();

	//start common module
	virtual bool StartMainModule(bool force = false);

	//exit common module
	virtual bool ExitMainModule(bool force = false);

	//этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual void PrepareNames() const;
	//вызов метода
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	virtual long FindProp(const wxString& strName) const;
};

class BACKEND_API CModuleManagerExternalReport : public IModuleManager {
	CRecordDataObjectReport* m_objectValue;
public:

	virtual CCompileModule* GetCompileModule() const;
	virtual CProcUnit* GetProcUnit() const;

	virtual std::map<wxString, CValue*>& GetContextVariables();

	//metaData and external variant
	CModuleManagerExternalReport(IMetaData* metaData = nullptr, CMetaObjectReport* metaObject = nullptr);
	virtual ~CModuleManagerExternalReport();

	//return external module
	virtual IRecordDataObjectExt* GetObjectValue() const {
		return m_objectValue;
	}

	//Create common module
	virtual bool CreateMainModule();

	//destroy common module
	virtual bool DestroyMainModule();

	//start common module
	virtual bool StartMainModule(bool force = false);

	//exit common module
	virtual bool ExitMainModule(bool force = false);

	//этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual void PrepareNames() const;
	//вызов метода
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута
	virtual long FindProp(const wxString& strName) const;
};

#endif

