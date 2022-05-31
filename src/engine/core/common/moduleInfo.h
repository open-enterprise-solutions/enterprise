#ifndef _MODULEINFO_H__
#define _MODULEINFO_H__

#include "compiler/procUnit.h"

class IModuleInfo
{
public:

	CMetaModuleObject* GetMetaObject() const {
		return GetCompileModule() ? GetCompileModule()->GetModuleObject() : NULL;
	}

	//גûחמג לועמהא
	CValue ExecuteMethod(methodArg_t& aParams);

	IModuleInfo();
	IModuleInfo(CCompileModule* compileModule);
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