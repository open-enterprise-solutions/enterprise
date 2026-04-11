#ifndef __MODULE_MANAGER_H__
#define __MODULE_MANAGER_H__

#include "backend/moduleInfo.h"

#include "backend/metaCollection/metaObjectMetadata.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metaCollection/partial/commonObject.h"

class BACKEND_API ibValueModuleManager :
	public ibModuleDataObject, public ibValue {
protected:
	enum helperAlias {
		eProcUnit
	};
public:

	class BACKEND_API ibValueModuleUnit :
		public ibModuleDataObject, public ibValue {
		wxDECLARE_DYNAMIC_CLASS(ibValueModuleUnit);
	protected:
		enum helperAlias {
			eProcUnit
		};
	public:

		ibValueModuleUnit() {}
		ibValueModuleUnit(ibValueModuleManager* moduleManager, ibValueMetaObjectModuleBase* moduleObject, bool managerModule = false);
		virtual ~ibValueModuleUnit();

		//initalize common module
		bool CreateCommonModule();
		bool DestroyCommonModule();

		//get common module 
		ibValueMetaObjectModuleBase* GetModuleObject() const {
			return m_moduleObject;
		}

		//Is global module?
		wxString GetModuleFullName() const {
			return m_moduleObject ? m_moduleObject->GetFullName() : wxString(wxEmptyString);
		}

		wxString GetModuleDocPath() const {
			return m_moduleObject ? m_moduleObject->GetDocPath() : wxString(wxEmptyString);
		}

		wxString GetModuleName() const {
			return m_moduleObject ? m_moduleObject->GetName() : wxString(wxEmptyString);
		}

		wxString GetModuleText() const {
			return m_moduleObject ? m_moduleObject->GetModuleText() : wxString(wxEmptyString);
		}

		bool IsGlobalModule() const {
			return m_moduleObject ? m_moduleObject->IsGlobalModule() : false;
		}

		//WORK AS AN AGGREGATE OBJECT

		// these methods need to be overridden in your aggregate objects:
		virtual ibValueMethodHelper* GetPMethods() const override { // get a reference to the class helper for parsing attribute and method names
			//PrepareNames();
			return m_methodHelper;
		}

		// this method is automatically called to initialize attribute and method names.
		virtual void PrepareNames() const override;

		//method call
		virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

		virtual wxString GetString() const override {
			return m_moduleObject->GetName();
		}

		//check is empty
		virtual bool IsEmpty() const override {
			return false;
		}

		//operator '=='
		virtual bool CompareValueEQ(const ibValue& cParam) const override
		{
			ibValueModuleUnit* compareModule = dynamic_cast<ibValueModuleUnit*>(cParam.GetRef());
			if (compareModule) {
				return m_moduleObject == compareModule->GetModuleObject();
			}

			return false;
		}

		//operator '!='
		virtual bool CompareValueNE(const ibValue& cParam) const override
		{
			ibValueModuleUnit* compareModule = dynamic_cast<ibValueModuleUnit*>(cParam.GetRef());
			if (compareModule) {
				return m_moduleObject != compareModule->GetModuleObject();
			}

			return false;
		}

	protected:
		ibValueModuleManager* m_moduleManager;
		ibValueMetaObjectModuleBase* m_moduleObject;
	private:
		ibValueMethodHelper* m_methodHelper;
	};

	class BACKEND_API ibValueMetadataUnit :
		public ibValue {
		wxDECLARE_DYNAMIC_CLASS(ibValueMetadataUnit);
	public:

		ibValueMetadataUnit() {}
		ibValueMetadataUnit(ibMetaData* metaData);
		virtual ~ibValueMetadataUnit();

		//get common module 
		ibMetaData* GetMetaData() const { return m_metaData; }

		//check is empty
		virtual bool IsEmpty() const override { return false; }

		//operator '=='
		virtual bool CompareValueEQ(const ibValue& cParam) const override
		{
			ibValueMetadataUnit* compareMetadata = dynamic_cast<ibValueMetadataUnit*>(cParam.GetRef());
			if (compareMetadata) {
				return m_metaData == compareMetadata->GetMetaData();
			}

			return false;
		}

		//operator '!='
		virtual bool CompareValueNE(const ibValue& cParam) const override {
			ibValueMetadataUnit* compareMetadata = dynamic_cast<ibValueMetadataUnit*>(cParam.GetRef());
			if (compareMetadata) {
				return m_metaData != compareMetadata->GetMetaData();
			}

			return false;
		}

		// these methods need to be overridden in your aggregate objects:
		virtual ibValueMethodHelper* GetPMethods() const override {
			//PrepareNames();
			return m_methodHelper;
		}

		virtual void PrepareNames() const override; // this method is automatically called to initialize attribute and method names.

		//****************************************************************************
		//*                              Override attribute                          *
		//****************************************************************************

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;        //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;                   //attribute value

	private:
		ibMetaData* m_metaData;
		ibValueMethodHelper* m_methodHelper;
	};

private:

	void Clear();

protected:

	//metaData and external variant
	ibValueModuleManager(ibMetaData* metaData, ibValueMetaObjectModule* metaObject);

public:

	virtual ~ibValueModuleManager();

	//Create common module
	virtual bool CreateMainModule() = 0;

	//destroy common module
	virtual bool DestroyMainModule() = 0;

	//start common module
	virtual bool StartMainModule(bool force = false) = 0;

	//exit common module
	virtual bool ExitMainModule(bool force = false) = 0;

	// these methods need to be overridden in your aggregate objects:
	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}

	// this method is automatically called to initialize attribute and method names.
	virtual void PrepareNames() const;

	//method call
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value
	virtual long FindProp(const wxString& strName) const;

	//check is empty
	virtual bool IsEmpty() const { return false; }

	//compile modules:
	bool AddCompileModule(const ibValueMetaObject* moduleObject, ibValue* object);
	bool RemoveCompileModule(const ibValueMetaObject* moduleObject);

	//templates:
	template <class T> inline bool FindCompileModule(const ibValueMetaObject* moduleObject, T*& objValue) const {
		auto it = m_listCommonModuleValue.find(moduleObject);
		if (it != m_listCommonModuleValue.end()) {
			objValue = dynamic_cast<T*>(&(*it->second));
			return objValue != nullptr;
		}
		objValue = nullptr;
		return false;
	}

	template <class T> inline bool FindParentCompileModule(const ibValueMetaObject* moduleObject, T*& objValue) const {
		ibValueMetaObject* parentMetadata = moduleObject ? moduleObject->GetParent() : nullptr;
		if (parentMetadata != nullptr)
			return FindCompileModule(parentMetadata, objValue);
		return false;
	}

	//common modules:
	bool AddCommonModule(ibValueMetaObjectCommonModule* commonModule, bool managerModule = false, bool runModule = false);

	ibValueModuleUnit* FindCommonModule(ibValueMetaObjectCommonModule* commonModule) const;
	bool RenameCommonModule(ibValueMetaObjectCommonModule* commonModule, const wxString& newName);
	bool RemoveCommonModule(ibValueMetaObjectCommonModule* commonModule);

	//system object:
	ibValue* GetObjectManager() const { return m_objectManager; }
	ibValueMetadataUnit* GetMetaManager() const { return m_metaManager; }

	virtual std::vector<ibValuePtr<ibValueModuleUnit>>& GetCommonModules() { return m_listCommonModuleManager; }

	//associated map
	virtual std::map<wxString, ibValuePtr<ibValue>>& GetGlobalVariables() { return m_listGlConstValue; }
	virtual std::map<wxString, ibValue*>& GetContextVariables() { return m_compileModule->m_listContextValue; }

	//return external module
	virtual ibValue* GetObjectValue() const { return nullptr; }

protected:

	bool m_initialized;

	//global manager
	ibValuePtr<ibValue> m_objectManager;

	// global metamanager
	ibValuePtr<ibValueMetadataUnit> m_metaManager;

	//map with compile data
	std::map<const ibValueMetaObject*, ibValuePtr<ibValue>> m_listCommonModuleValue;

	//array of common modules
	std::vector<ibValuePtr<ibValueModuleUnit>> m_listCommonModuleManager;

	//array of global variables
	std::map<wxString, ibValuePtr<ibValue>> m_listGlConstValue;

	friend class ibMetaDataConfiguration;
	friend class ibMetaDataDataProcessor;

	friend class ibValueModuleUnit;

	ibValueMethodHelper* m_methodHelper;
};

class BACKEND_API ibValueModuleManagerConfiguration :
	public ibValueModuleManager {
	//system events:
	bool BeforeStart();
	void OnStart();
	bool BeforeExit();
	void OnExit();
public:

	//metaData and external variant
	ibValueModuleManagerConfiguration(ibMetaData* metaData = nullptr, ibValueMetaObjectConfiguration* metaObject = nullptr);

	//Create common module
	virtual bool CreateMainModule();

	//destroy common module
	virtual bool DestroyMainModule();

	//start common module
	virtual bool StartMainModule(bool force = false);

	//exit common module
	virtual bool ExitMainModule(bool force = false);
};

#endif

