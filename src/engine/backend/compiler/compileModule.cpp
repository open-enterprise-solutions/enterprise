#include "compileModule.h"

#include <stack>

#pragma warning(push)
#pragma warning(disable : 4018)

ibCompileModule::ibCompileModule(const ibCompilableSourceCode* sourceCode, const ibBindTable& binds, std::unique_ptr<ibCompileModule> parent) :
	ibCompileCode(sourceCode->GetFullName(), sourceCode->GetDocPath(), sourceCode->IsCommonModule()),
	m_sourceCode(sourceCode),
	m_parentModule(std::move(parent))
{
	m_binds = binds;

	// New, so it has compiled nothing: the walk over a chain of them (Compile) goes by this flag.
	m_changedCode = true;

	m_cByteCode.m_strModuleName = m_sourceCode->GetFullName();

	m_translateCode.Load(*m_sourceCode);

	// A global module takes no compiler: its lexems are read in front of this text and compile with it.
	for (const auto& kv : m_binds.m_listGlobalModule) {
		auto translateCode = std::make_unique<ibTranslateCode>(GetCodeStyle());
		translateCode->Load(*kv.second);
		m_translateCode.AppendModule(translateCode.get());
		m_listTranslateCode.emplace_back(std::move(translateCode));
	}

	//We don’t look for local variables in parent contexts!
	m_rootContext->m_numFindLocalInParent = 0;

	if (m_parentModule != nullptr)
		SetParent(m_parentModule.get());
}

/**
 * Compile
 * Purpose:
 * Translation and compilation of source code into bytecode (object code)
 * Return value:
 * true,false
 */

bool ibCompileModule::Compile()
{
	//clear functions & variables 
	Reset();

	if (m_sourceCode != nullptr &&
		m_sourceCode->IsGlobalModule()) {

		m_changedCode = false;

		m_translateCode.Load(*m_sourceCode);

		return m_parentModule != nullptr ?
			m_parentModule->Compile() : true;
	}

	//recursively compile modules in case of any changes
	if (m_parentModule != nullptr) {

		std::stack<ibCompileModule*> compileModule; bool callRecompile = false;

		ibCompileModule* parentModule = GetParent();

		while (parentModule != nullptr) {
			if (parentModule->m_changedCode) callRecompile = true;
			if (callRecompile) compileModule.push(parentModule);
			parentModule = parentModule->GetParent();
		}

		while (!compileModule.empty()) {
			ibCompileModule* compileCode = compileModule.top();
			if (!compileCode->Recompile()) return false;
			compileModule.pop();
		}
	}

	if (m_sourceCode != nullptr) {

		m_cByteCode.m_strModuleName = m_sourceCode->GetFullName();

		if (m_parentModule != nullptr) {
			m_cByteCode.m_parent = &m_parentModule->m_cByteCode;
			m_rootContext->m_parentContext = m_parentModule->GetContext();
		}

		m_translateCode.Load(*m_sourceCode);
	}

	//prepare lexem
	if (!Translate()) {
		return false;
	}

	//prepare context variables
	PrepareModuleData();

	// compilation
	if (CompileModule()) {
		m_changedCode = false;
		return true;
	}

	return false;
}

/**
 * Recompile
 * Purpose:
 * Translation and recompilation of the current source code into bytecode (object code)
 * Return value:
 * true,false
 */

bool ibCompileModule::Recompile()
{
	//clear functions & variables 
	Reset();

	if (m_parentModule != nullptr) {

		if (m_sourceCode != nullptr &&
			m_sourceCode->IsGlobalModule()) {

			m_changedCode = false;

			m_translateCode.Load(*m_sourceCode);

			return m_parentModule != nullptr ?
				m_parentModule->Compile() : true;
		}
	}

	if (m_sourceCode != nullptr) {

		m_cByteCode.m_strModuleName = m_sourceCode->GetFullName();

		if (m_parentModule) {
			m_cByteCode.m_parent = &m_parentModule->m_cByteCode;
			m_rootContext->m_parentContext = m_parentModule->GetContext();
		}

		m_translateCode.Load(*m_sourceCode);
	}

	//prepare lexem 
	if (!Translate()) {
		return false;
	}

	//prepare context variables
	PrepareModuleData();

	// compilation 
	if (CompileModule()) {
		m_changedCode = false;
		return true;
	}

	m_changedCode = true;
	return false;
}

#pragma warning(pop)