#ifndef _MODULE_MANAGER_EXT_H__
#define _MODULE_MANAGER_EXT_H__

#include "backend/moduleManager/moduleManager.h"
#include "backend/metaCollection/partial/dataTool.h"
#include "backend/metaCollection/partial/dataReport.h"

class BACKEND_API ibValueModuleRuntimeManagerExternalDataTool : public ibValueModuleRuntimeManager {
	public:

	virtual const ibByteCode* GetByteCode() const override;
	virtual std::unique_ptr<ibCompileModule> CreateCompiler() const override;
	virtual std::shared_ptr<ibProcUnit> GetProcUnit() const;

	virtual std::map<ibString, ibContextVar>& GetContextVariables();
	virtual std::map<ibString, ibValue*>& GetGlobalVariables();

	//metaData and external variant
	ibValueModuleRuntimeManagerExternalDataTool(std::shared_ptr<ibMetaData> metaData = nullptr, ibValueMetaObjectDataTool* metaObject = nullptr);
	virtual ~ibValueModuleRuntimeManagerExternalDataTool();

	//return external module
	virtual ibValueRecordDataObjectExt* GetObjectValue() const { return m_objectValue; }

	//Create shared module
	virtual bool CreateMainModule();

	//destroy shared module
	virtual bool DestroyMainModule();

	//start shared module
	virtual bool StartMainModule(const ibRuntimeRequest& request = ibRuntimeRequest());

	//exit shared module
	virtual bool ExitMainModule(const ibRuntimeRequest& request = ibRuntimeRequest());

	// Copies the external object value's surface after the module's names (ibValueModuleDataObject binds
	// first). Bound in the ctor.
	void FillMembers(ibMemberTable& helper) const;

	//method call
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);
	virtual bool GetParamDefValue(const long lMethodNum, const long lParamNum, ibValue& pvarParamDefValue) const override;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

private:
	ibValueRecordDataObjectDataTool* m_objectValue;
};

class BACKEND_API ibValueModuleRuntimeManagerExternalReport : public ibValueModuleRuntimeManager {
	public:

	virtual const ibByteCode* GetByteCode() const override;
	virtual std::unique_ptr<ibCompileModule> CreateCompiler() const override;
	virtual std::shared_ptr<ibProcUnit> GetProcUnit() const;

	virtual std::map<ibString, ibContextVar>& GetContextVariables();
	virtual std::map<ibString, ibValue*>& GetGlobalVariables();

	//metaData and external variant
	ibValueModuleRuntimeManagerExternalReport(std::shared_ptr<ibMetaData> metaData = nullptr, ibValueMetaObjectReport* metaObject = nullptr);
	virtual ~ibValueModuleRuntimeManagerExternalReport();

	//return external module
	virtual ibValueRecordDataObjectExt* GetObjectValue() const { return m_objectValue; }

	//Create shared module
	virtual bool CreateMainModule();

	//destroy shared module
	virtual bool DestroyMainModule();

	//start shared module
	virtual bool StartMainModule(const ibRuntimeRequest& request = ibRuntimeRequest());

	//exit shared module
	virtual bool ExitMainModule(const ibRuntimeRequest& request = ibRuntimeRequest());

	// Copies the external object value's surface after the module's names (ibValueModuleDataObject binds
	// first). Bound in the ctor.
	void FillMembers(ibMemberTable& helper) const;

	//method call
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);
	virtual bool GetParamDefValue(const long lMethodNum, const long lParamNum, ibValue& pvarParamDefValue) const override;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

private:
	ibValueRecordDataObjectReport* m_objectValue;
};

#endif