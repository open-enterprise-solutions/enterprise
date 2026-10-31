#ifndef _COMPILE_MODULE_H__
#define _COMPILE_MODULE_H__

#include "backend/backend.h"
#include "backend/compiler/compileCode.h"

class BACKEND_API ibCompileModule : public ibCompileCode {
public:
	virtual bool Recompile(); // recompile current module from its text
	virtual bool Compile(); // compile current module from its text
public:

	// `parent`: the compiler above this one in a chain, made for this compile and gone with it. Whoever compiles
	// against a parent that is already compiled gives its bytecode instead (SetParent).
	ibCompileModule(const ibCompilableSourceCode* sourceCode, const ibBindTable& binds, std::unique_ptr<ibCompileModule> parent = nullptr);
	virtual ~ibCompileModule() {}

	ibCompileModule* GetParent() const { return m_parentModule.get(); }

private:
	const ibCompilableSourceCode* m_sourceCode;

	std::unique_ptr<ibCompileModule> m_parentModule;

	// A reader of each bound global module: its lexems point back at it, so it stays for as long as this compile.
	std::vector<std::unique_ptr<ibTranslateCode>> m_listTranslateCode;
};

#endif
