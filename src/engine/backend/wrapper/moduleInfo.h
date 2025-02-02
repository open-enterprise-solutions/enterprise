#ifndef _MODULE_INFO_H__
#define _MODULE_INFO_H__

#include "backend/compiler/compileModule.h"
#include "backend/compiler/procUnit.h"

class BACKEND_API IModuleInfo {
public:

	CMetaObjectModule* GetMetaObject() const {
		return GetCompileModule() ? GetCompileModule()->GetModuleObject() : nullptr;
	}

	//גûחמג לועמהא
	bool ExecuteProc(const wxString& strMethodName,
		CValue** paParams,
		const long lSizeArray);

	bool ExecuteFunc(const wxString& strMethodName,
		CValue& pvarRetValue,
		CValue** paParams,
		const long lSizeArray);

	IModuleInfo();
	IModuleInfo(CCompileModule* compileCode);
	virtual ~IModuleInfo();

	virtual CCompileModule* GetCompileModule() const {
		return m_compileModule;
	}

	virtual CProcUnit* GetProcUnit() const {
		return m_procUnit;
	}

protected:
	CCompileModule* m_compileModule;
	CProcUnit* m_procUnit;
};

#endif 