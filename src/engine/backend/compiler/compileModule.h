#ifndef _COMPILE_MODULE_H__
#define _COMPILE_MODULE_H__

#include "backend/backend.h"
#include "backend/compiler/compileCode.h"

//////////////////////////////////////////////////////////////////////
class BACKEND_API ibValueMetaObjectModuleBase;
//////////////////////////////////////////////////////////////////////

class BACKEND_API ibCompileModule : public ibCompileCode {
public:
	virtual bool Recompile(); // recompile current module from meta-object
	virtual bool Compile(); // compile current module from meta-object
public:

	ibCompileModule(const ibValueMetaObjectModuleBase* moduleObject, bool onlyFunction = false);
	virtual ~ibCompileModule();

	// Parent compile-module — orchestration storage for the compile
	// cascade (global module recurse, designer recompile, re-set
	// bytecode parent on Reset). Read via GetParent(); m_parentModule
	// is private, set only via SetParent.
	ibCompileModule* GetParent() const { return m_parentModule; }
	void SetParent(ibCompileCode* parent) /* shadow base — also stores the typed parent */ {
		ibCompileCode::SetParent(parent);
		m_parentModule = dynamic_cast<ibCompileModule*>(parent);
	}

	virtual const ibValueMetaObjectModuleBase* GetObjectModule() const { return m_moduleObject; }

protected:
	const ibValueMetaObjectModuleBase* m_moduleObject;

private:
	ibCompileModule* m_parentModule = nullptr;
};

class BACKEND_API ibCompileCommonModule : public ibCompileModule {
public:
	ibCompileCommonModule(const ibValueMetaObjectModuleBase* moduleObject) :
		ibCompileModule(moduleObject, true) {
	}
};

class BACKEND_API ibCompileGlobalModule : public ibCompileCommonModule {
public:
	ibCompileGlobalModule(const ibValueMetaObjectModuleBase* moduleObject) :
		ibCompileCommonModule(moduleObject) {
	}
};

#endif