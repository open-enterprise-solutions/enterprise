#include "compileModule.h"
#include "backend/metaCollection/metaModuleObject.h"

#pragma warning(push)
#pragma warning(disable : 4018)

CCompileModule::CCompileModule(CMetaObjectModule* moduleObject, bool onlyFunction) :
	CCompileCode(moduleObject->GetFullName(), moduleObject->GetDocPath()),
	m_moduleObject(moduleObject)
{
	InitializeCompileModule();

	m_cByteCode.m_strModuleName = m_moduleObject->GetFullName();

	m_strModuleName = m_moduleObject->GetFullName();
	m_strDocPath = m_moduleObject->GetDocPath();
	m_strFileName = m_moduleObject->GetFileName();

	Load(m_moduleObject->GetModuleText());

	//у родительских контекстов локальные переменные не ищем!
	m_cContext.m_nFindLocalInParent = 0;
}

CMetaObjectModule* CCompileModule::GetModuleObject() const
{
	return m_moduleObject;
}

CCompileModule* CCompileModule::GetParent() const
{
	return dynamic_cast<CCompileModule*>(m_parent);
}

/**
 * Compile
 * Назначение:
 * Трасляция и компиляция исходного кода в байт-код (объектный код)
 * Возвращаемое значение:
 * true,false
 */
bool CCompileModule::Compile()
{
	//clear functions & variables 
	Reset();

	if (m_parent != nullptr) {

		if (m_moduleObject &&
			m_moduleObject->IsGlobalModule()) {

			m_strModuleName = m_moduleObject->GetFullName();
			m_strDocPath = m_moduleObject->GetDocPath();
			m_strFileName = m_moduleObject->GetFileName();

			m_changeCode = false;

			Load(m_moduleObject->GetModuleText());

			return m_parent != nullptr ?
				((CCompileModule*)m_parent)->Compile() : true;
		}
	}

	//контекст самого модуля
	m_pContext = GetContext();

	//рекурсивно компилируем модули на случай каких-либо изменений 
	if (m_parent != nullptr) {

		bool callRecompile = false;
		std::stack<CCompileModule*> compileModules;
		CCompileModule* parentModule = dynamic_cast<CCompileModule*>(m_parent);

		while (parentModule != nullptr) {
			if (parentModule->m_changeCode) {
				callRecompile = true;
			}
			if (callRecompile) {
				compileModules.push(parentModule);
			}
			parentModule = dynamic_cast<CCompileModule*>(parentModule->GetParent());
		}

		while (!compileModules.empty()) {
			CCompileModule* compileCode = compileModules.top();
			if (!compileCode->Recompile()) {
				return false;
			}
			compileModules.pop();
		}
	}

	if (m_moduleObject != nullptr) {

		m_cByteCode.m_strModuleName = m_moduleObject->GetFullName();

		if (m_parent != nullptr) {
			m_cByteCode.m_parent = &m_parent->m_cByteCode;
			m_cContext.m_parentContext = &m_parent->m_cContext;
		}

		m_strModuleName = m_moduleObject->GetFullName();
		m_strDocPath = m_moduleObject->GetDocPath();
		m_strFileName = m_moduleObject->GetFileName();

		Load(m_moduleObject->GetModuleText());
	}

	//prepare lexem 
	if (!PrepareLexem()) {
		return false;
	}

	//подготовить контекстные переменные 
	PrepareModuleData();

	//Компиляция 
	if (CompileModule()) {
		m_changeCode = false;
		return true;
	}

	return false;
}

/**
 * Recompile
 * Назначение:
 * Трасляция и перекомпиляция текущего исходного кода в байт-код (объектный код)
 * Возвращаемое значение:
 * true,false
 */
bool CCompileModule::Recompile()
{
	//clear functions & variables 
	Reset();

	if (m_parent != nullptr) {
		if (m_moduleObject &&
			m_moduleObject->IsGlobalModule()) {

			m_strModuleName = m_moduleObject->GetFullName();
			m_strDocPath = m_moduleObject->GetDocPath();
			m_strFileName = m_moduleObject->GetFileName();

			m_changeCode = false;

			Load(m_moduleObject->GetModuleText());

			return m_parent != nullptr ?
				((CCompileModule*)m_parent)->Compile() : true;
		}
	}

	//контекст самого модуля
	m_pContext = GetContext();

	if (m_moduleObject) {
		m_cByteCode.m_strModuleName = m_moduleObject->GetFullName();

		if (m_parent) {
			m_cByteCode.m_parent = &m_parent->m_cByteCode;
			m_cContext.m_parentContext = &m_parent->m_cContext;
		}

		m_strModuleName = m_moduleObject->GetFullName();
		m_strDocPath = m_moduleObject->GetDocPath();
		m_strFileName = m_moduleObject->GetFileName();

		Load(m_moduleObject->GetModuleText());
	}

	//prepare lexem 
	if (!PrepareLexem()) {
		return false;
	}

	//подготовить контекстные переменные 
	PrepareModuleData();

	//Компиляция 
	if (CompileModule()) {
		m_changeCode = false;
		return true;
	}

	m_changeCode = true;
	return false;
}

#pragma warning(pop)