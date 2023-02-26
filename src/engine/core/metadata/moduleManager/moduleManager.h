#ifndef _MODULE_MANAGER_H__
#define _MODULE_MANAGER_H__

#include "core/common/moduleInfo.h"
#include "core/metadata/metaObjects/metaObjectMetadata.h"
#include "core/metadata/metaObjects/metaModuleObject.h"
#include "core/metadata/metaObjects/objects/dataProcessor.h"
#include "core/metadata/metaObjects/objects/dataReport.h"

class IModuleManager : public CValue,
	public IModuleInfo {
protected:
	enum helperAlias {
		eProcUnit
	};
public:
	class CModuleValue : public CValue,
		public IModuleInfo {
		wxDECLARE_DYNAMIC_CLASS(CModuleValue);
	protected:
		enum helperAlias {
			eProcUnit
		};
	private:
		CMethodHelper* m_methodHelper;
	public:

		CModuleValue() {}
		CModuleValue(IModuleManager* moduleManager, CMetaModuleObject* moduleObject, bool managerModule = false);
		virtual ~CModuleValue();

		//initalize common module
		bool CreateCommonModule();
		bool DestroyCommonModule();

		//get common module 
		CMetaModuleObject* GetModuleObject() const {
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
			PrepareNames();
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

		virtual wxString GetTypeString() const override {
			return wxT("module");
		}

		//check is empty
		virtual inline bool IsEmpty() const override {
			return false;
		}

		//operator '=='
		virtual inline bool CompareValueEQ(const CValue& cParam) const override
		{
			CModuleValue* compareModule = dynamic_cast<CModuleValue*>(cParam.GetRef());
			if (compareModule) {
				return m_moduleObject == compareModule->GetModuleObject();
			}

			return false;
		}

		//operator '!='
		virtual inline bool CompareValueNE(const CValue& cParam) const override
		{
			CModuleValue* compareModule = dynamic_cast<CModuleValue*>(cParam.GetRef());
			if (compareModule) {
				return m_moduleObject != compareModule->GetModuleObject();
			}

			return false;
		}

	protected:
		IModuleManager* m_moduleManager;
		CMetaModuleObject* m_moduleObject;
	};

	class CMetadataValue : public CValue {
		wxDECLARE_DYNAMIC_CLASS(CMetadataValue);
	private:
		CMethodHelper* m_methodHelper;
	public:

		CMetadataValue() {}
		CMetadataValue(IMetadata* metaData);
		virtual ~CMetadataValue();

		//get common module 
		IMetadata* GetMetadata() const { 
			return m_metaData;
		}

		virtual wxString GetTypeString() const override {
			return wxT("metadata");
		}

		virtual wxString GetString() const override {
			return wxT("metadata");
		}

		//check is empty
		virtual inline bool IsEmpty() const override {
			return false;
		}

		//operator '=='
		virtual inline bool CompareValueEQ(const CValue& cParam) const override
		{
			CMetadataValue* compareMetadata = dynamic_cast<CMetadataValue*>(cParam.GetRef());
			if (compareMetadata) {
				return m_metaData == compareMetadata->GetMetadata();
			}

			return false;
		}

		//operator '!='
		virtual inline bool CompareValueNE(const CValue& cParam) const override {
			CMetadataValue* compareMetadata = dynamic_cast<CMetadataValue*>(cParam.GetRef());
			if (compareMetadata) {
				return m_metaData != compareMetadata->GetMetadata();
			}

			return false;
		}

		//эти методы нужно переопределить в ваших агрегатных объектах:
		virtual CMethodHelper* GetPMethods() const override { 
			PrepareNames();  
			return m_methodHelper; }
		
		virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов

		//****************************************************************************
		//*                              Override attribute                          *
		//****************************************************************************

		virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal) override;        //установка атрибута
		virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal) override;                   //значение атрибута

	protected:
		IMetadata* m_metaData;
	};

private:

	void Clear();

protected:

	//metadata and external variant
	IModuleManager(IMetadata* metaData, CMetaModuleObject* metaObject);

public:

	virtual ~IModuleManager();

	//Create common module
	virtual bool CreateMainModule() = 0;

	//destroy common module
	virtual bool DestroyMainModule() = 0;

	//start common module
	virtual bool StartMainModule() = 0;

	//exit common module
	virtual bool ExitMainModule(bool force = false) = 0;

	//эти методы нужно переопределить в ваших агрегатных объектах:
	virtual CMethodHelper* GetPMethods() const { PrepareNames();  return m_methodHelper; }//получить ссылку на класс помощник разбора имен атрибутов и методов
	//этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual void PrepareNames() const;
	//вызов метода
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута
	virtual long FindProp(const wxString& sName) const;

	virtual wxString GetString() const {
		return wxT("moduleManager");
	}

	virtual wxString GetTypeString() const {
		return wxT("moduleManager");
	}

	//check is empty
	virtual inline bool IsEmpty() const override {
		return false;
	}

	//compile modules:
	bool AddCompileModule(IMetaObject* moduleObject, CValue* object);
	bool RemoveCompileModule(IMetaObject* moduleObject);

	//templates:
	template <class T> inline bool FindCompileModule(IMetaObject* moduleObject, T*& objValue) const {
		auto foundedIt = m_aCompileModules.find(moduleObject);
		if (foundedIt != m_aCompileModules.end()) {
			objValue = dynamic_cast<T*>(foundedIt->second);
			return objValue != NULL;
		}
		objValue = NULL;
		return false;
	}

	template <class T> inline bool FindParentCompileModule(IMetaObject* moduleObject, T*& objValue) const {
		IMetaObject* parentMetadata = moduleObject ? moduleObject->GetParent() : NULL;
		if (parentMetadata != NULL)
			return FindCompileModule(parentMetadata, objValue);
		return false;
	}

	//common modules:
	bool AddCommonModule(CMetaCommonModuleObject* commonModule, bool managerModule = false, bool runModule = false);

	CModuleValue* FindCommonModule(CMetaCommonModuleObject* commonModule) const;
	bool RenameCommonModule(CMetaCommonModuleObject* commonModule, const wxString& newName);
	bool RemoveCommonModule(CMetaCommonModuleObject* commonModule);

	//system object:
	CValue* GetObjectManager() const {
		return m_objectManager;
	}

	CValue* GetSysObjectManager() const {
		return m_objectSysManager;
	}

	CMetadataValue* GetMetaManager() const {
		return m_metaManager;
	}

	virtual std::vector<CModuleValue*>& GetCommonModules() {
		return m_aCommonModules;
	}

	//associated map
	virtual std::map<wxString, CValue*>& GetGlobalVariables() {
		return m_aValueGlVariables;
	}

	virtual std::map<wxString, CValue*>& GetContextVariables() {
		return m_compileModule->m_aContextValues;
	}

	//return external module
	virtual IRecordDataObjectExt* GetObjectValue() const {
		return NULL;
	}

protected:

	bool m_initialized;

	//global manager
	CValue* m_objectManager;
	//global sysmanager 
	CValue* m_objectSysManager;
	// global metamanager
	CMetadataValue* m_metaManager;

	//map with compile data
	std::map<IMetaObject*, CValue*> m_aCompileModules;
	//array of common modules
	std::vector<CModuleValue*> m_aCommonModules;
	//array of global variables
	std::map<wxString, CValue*> m_aValueGlVariables;

	friend class CConfigMetadata;
	friend class CMetadataDataProcessor;

	friend class CModuleValue;

	CMethodHelper* m_methodHelper;
};

class CModuleManager : public IModuleManager {
	//system events:
	bool BeforeStart();
	void OnStart();
	bool BeforeExit();
	void OnExit();
public:

	//metadata and external variant
	CModuleManager(IMetadata* metaData, CMetaObject* metaObject);

	//Create common module
	virtual bool CreateMainModule();

	//destroy common module
	virtual bool DestroyMainModule();

	//start common module
	virtual bool StartMainModule();

	//exit common module
	virtual bool ExitMainModule(bool force = false);
};

class CExternalDataProcessorModuleManager : public IModuleManager {
	CObjectDataProcessor* m_objectValue;
public:

	virtual CCompileModule* GetCompileModule() const;
	virtual CProcUnit* GetProcUnit() const;

	virtual std::map<wxString, CValue*>& GetContextVariables();

	//metadata and external variant
	CExternalDataProcessorModuleManager(IMetadata* metaData, CMetaObjectDataProcessor* metaObject);
	virtual ~CExternalDataProcessorModuleManager();

	//return external module
	virtual IRecordDataObjectExt* GetObjectValue() const {
		return m_objectValue;
	}

	//Create common module
	virtual bool CreateMainModule();

	//destroy common module
	virtual bool DestroyMainModule();

	//start common module
	virtual bool StartMainModule();

	//exit common module
	virtual bool ExitMainModule(bool force = false);

	//этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual void PrepareNames() const;
	//вызов метода
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	virtual long FindProp(const wxString& sName) const;
};

class CExternalReportModuleManager : public IModuleManager {
	CObjectReport* m_objectValue;
public:

	virtual CCompileModule* GetCompileModule() const;
	virtual CProcUnit* GetProcUnit() const;

	virtual std::map<wxString, CValue*>& GetContextVariables();

	//metadata and external variant
	CExternalReportModuleManager(IMetadata* metaData, CMetaObjectReport* metaObject);
	virtual ~CExternalReportModuleManager();

	//return external module
	virtual IRecordDataObjectExt* GetObjectValue() const {
		return m_objectValue;
	}

	//Create common module
	virtual bool CreateMainModule();

	//destroy common module
	virtual bool DestroyMainModule();

	//start common module
	virtual bool StartMainModule();

	//exit common module
	virtual bool ExitMainModule(bool force = false);

	//этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual void PrepareNames() const;
	//вызов метода
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута
	virtual long FindProp(const wxString& sName) const;
};

#endif

