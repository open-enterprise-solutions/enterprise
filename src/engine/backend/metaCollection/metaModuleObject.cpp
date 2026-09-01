////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metamodule object
////////////////////////////////////////////////////////////////////////////

#include "metaModuleObject.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/appData.h"
#include "backend/metaData.h" // ibCompileValueCache::GetModuleManager (designer compile-cache)
#include "backend/compiler/cache/byteCodeCache.h"

//***********************************************************************
//*                           ModuleObject                              *
//***********************************************************************



//***********************************************************************
//*                           System metaData                           *
//***********************************************************************

#include "backend/debugger/debugClient.h"

bool ibValueMetaObjectModuleBase::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	return ibValueMetaObject::OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectModuleBase::OnLoadMetaObject(ibMetaData* metaData)
{
	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectModuleBase::OnSaveMetaObject(int flags)
{
	// ⚠ ON THE DATABASE APPLY, AND DELIBERATELY NOT ON A PLAIN SAVE. A working variant is stored
	// for the designer's own sake; the running application never reads it, so the cached blob has
	// not diverged from anything and dropping it would discard a row that is still true. The
	// configuration the runtime reads changes at the APPLY — restructuring and all — and that is
	// the moment the two can disagree.
	//
	//save debugger client offset
	if ((flags & saveConfigFlag) != 0 && appData->DesignerMode()) {
		const wxString& strBuffer = GetModuleText();
		debugClient->SaveModule(GetDocPath(),
			1 + std::count(strBuffer.begin(), strBuffer.end(), wxT('\n')));
		// AOT cache row for this descriptor is now stale — Designer
		// just persisted new source text. Drop the row so the next
		// runtime session that compiles this module refreshes the
		// blob via the cache-miss path in
		// ibRuntimeModuleDataObject::Compile.
		ibByteCodeCache::Invalidate(GetGuid());
	}

	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectModuleBase::OnDeleteMetaObject()
{
	if (appData->DesignerMode()) {
		// Hygiene — orphan rows survive harmlessly (no descriptor
		// queries them again) but bloating sys_bytecode_cache serves
		// no purpose.
		ibByteCodeCache::Invalidate(GetGuid());
	}
	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectModuleBase::OnBeforeRunMetaObject(int flags)
{
	//initialize debugger client
	if ((flags & loadConfigFlag) == 0 && (flags & newObjectFlag) == 0 && appData->DesignerMode()) {
		const wxString& strBuffer = GetModuleText();
		debugClient->InitializeModule(GetDocPath(),
			1 + std::count(strBuffer.begin(), strBuffer.end(), wxT('\n')));
	}

	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectModuleBase::OnAfterCloseMetaObject()
{
	//remove debugger client module unit
	//if (appData->DesignerMode())
	//	debugClient->RemoveModule(GetDocPath());

	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                          default procedures						    *
//***********************************************************************

void ibValueMetaObjectModuleBase::SetDefaultProcedure(const wxString& procname, const ibContentHelper& contentHelper, std::vector<wxString> args)
{
	m_contentHelper.insert_or_assign(procname, CContentData{ contentHelper , args });
}

void ibValueMetaObjectModuleBase::SetDefaultFunction(const wxString& funcname, std::vector<wxString> args)
{
	m_contentHelper.insert_or_assign(funcname, CContentData{ eFunctionHelper, args });
}

//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

bool ibValueMetaObjectModule::ReadData(const ibDataNode& node)
{
	m_propertyModule->SetNodeValue(node.GetProperty(m_propertyModule->GetName()));
	return true;
}

bool ibValueMetaObjectModule::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyModule->GetName(), m_propertyModule->GetNodeValue());
	return true;
}

//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

ibValueMetaObjectCommonModule::ibValueMetaObjectCommonModule(const wxString& name, const wxString& synonym, const wxString& comment) :
	ibValueMetaObjectModuleBase(name, synonym, comment)
{
}

bool ibValueMetaObjectCommonModule::ReadData(const ibDataNode& node)
{
	m_propertyModule->SetNodeValue(node.GetProperty(m_propertyModule->GetName()));
	m_propertyGlobalModule->SetNodeValue(node.GetProperty(m_propertyGlobalModule->GetName()));
	return true;
}

bool ibValueMetaObjectCommonModule::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyModule->GetName(), m_propertyModule->GetNodeValue());
	node.SetProperty(m_propertyGlobalModule->GetName(), m_propertyGlobalModule->GetNodeValue());
	return true;
}

//***********************************************************************
//*                          common value object                        *
//***********************************************************************

#include "backend/metaData.h"

bool ibValueMetaObjectCommonModule::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	return ibValueMetaObjectModuleBase::OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectCommonModule::OnLoadMetaObject(ibMetaData* metaData)
{
	return ibValueMetaObjectModuleBase::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectCommonModule::OnSaveMetaObject(int flags)
{
	return ibValueMetaObjectModuleBase::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectCommonModule::OnDeleteMetaObject()
{
	return ibValueMetaObjectModuleBase::OnDeleteMetaObject();
}

bool ibValueMetaObjectCommonModule::OnRenameMetaObject(const wxString& newName)
{
	// Runtime registry (metadata storage) — read by per-session managers.
	if (auto* storage = m_metaData->GetModuleStorage()) {
		if (!storage->RenameCommonModule(this, newName))
			return false;
	}

	// Designer registry — the editor's own compiled-unit holder.
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (auto* mgr = cc->GetModuleManager())
			mgr->RenameCommonModule(this, newName);
	}

	return ibValueMetaObjectModuleBase::OnRenameMetaObject(newName);
}

bool ibValueMetaObjectCommonModule::OnBeforeRunMetaObject(int flags)
{
	if (auto* storage = m_metaData->GetModuleStorage()) {
		if (!storage->AddCommonModule(this))
			return false;
	}

	// Designer registry: register a compiled lightweight unit. newObjectFlag = a
	// module just created in the designer → compile now; bulk load defers to
	// the manager's CreateMainModule.
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (auto* mgr = cc->GetModuleManager())
			if (!mgr->AddCommonModule(this, /*managerModule=*/false, (flags & newObjectFlag) != 0))
				return false;
	}

	return ibValueMetaObjectModuleBase::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectCommonModule::OnAfterRunMetaObject(int flags)
{
	return ibValueMetaObjectModuleBase::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectCommonModule::OnBeforeCloseMetaObject()
{
	if (auto* storage = m_metaData->GetModuleStorage()) {
		if (!storage->RemoveCommonModule(this))
			return false;
	}

	if (auto* cc = m_metaData->GetCompileCache()) {
		if (auto* mgr = cc->GetModuleManager())
			mgr->RemoveCommonModule(this);
	}

	// Was OnAfterCloseMetaObject — a long-standing copy/paste that fired the
	// after-hook from the before phase (and skipped the real before-hook).
	// ibValueMetaObjectManagerModule (the sibling) already does this correctly.
	return ibValueMetaObjectModuleBase::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectCommonModule::OnAfterCloseMetaObject()
{
	return ibValueMetaObjectModuleBase::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                          manager value object                       *
//***********************************************************************

bool ibValueMetaObjectManagerModule::OnBeforeRunMetaObject(int flags)
{
	if (auto* storage = m_metaData->GetModuleStorage()) {
		if (!storage->AddCommonModule(this))
			return false;
	}

	// Designer registry — a manager module registers as managerModule=true.
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (auto* mgr = cc->GetModuleManager())
			if (!mgr->AddCommonModule(this, /*managerModule=*/true, (flags & newObjectFlag) != 0))
				return false;
	}

	return ibValueMetaObjectModuleBase::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectManagerModule::OnAfterRunMetaObject(int flags)
{
	return ibValueMetaObjectModuleBase::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectManagerModule::OnBeforeCloseMetaObject()
{
	if (auto* storage = m_metaData->GetModuleStorage()) {
		if (!storage->RemoveCommonModule(this))
			return false;
	}

	if (auto* cc = m_metaData->GetCompileCache()) {
		if (auto* mgr = cc->GetModuleManager())
			mgr->RemoveCommonModule(this);
	}

	return ibValueMetaObjectModuleBase::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectManagerModule::OnAfterCloseMetaObject()
{
	return ibValueMetaObjectModuleBase::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectModule, "Module", g_metaModuleCLSID);

METADATA_TYPE_REGISTER(ibValueMetaObjectCommonModule, "CommonModule", g_metaCommonModuleCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectManagerModule, "ManagerModule", g_metaManagerCLSID);
