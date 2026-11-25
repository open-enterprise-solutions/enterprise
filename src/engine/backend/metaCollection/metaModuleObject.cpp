////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metamodule object
////////////////////////////////////////////////////////////////////////////

#include "metaModuleObject.h"
#include "core/serialize/dataBuilder.h"
#include "backend/appData.h"
#include "backend/metaData.h" // ibCompileValueCache::GetModuleManager (studio compile-cache)
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
	// for the studio's own sake; the running application never reads it, so the cached blob has
	// not diverged from anything and dropping it would discard a row that is still true. The
	// application the runtime reads changes at the APPLY — restructuring and all — and that is
	// the moment the two can disagree.
	//
	//save debugger client offset
	if ((flags & saveApplicationFlag) != 0 && appData->StudioMode()) {
		const ibString& strBuffer = GetModuleText();
		debugClient->SaveModule(GetDocPath(),
			1 + std::count(strBuffer.begin(), strBuffer.end(), ibT('\n')));
		// AOT cache row for this descriptor is now stale — Studio
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
	if (appData->StudioMode()) {
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
	if ((flags & loadApplicationFlag) == 0 && (flags & newObjectFlag) == 0 && appData->StudioMode()) {
		const ibString& strBuffer = GetModuleText();
		debugClient->InitializeModule(GetDocPath(),
			1 + std::count(strBuffer.begin(), strBuffer.end(), ibT('\n')));
	}

	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectModuleBase::OnAfterCloseMetaObject()
{
	//remove debugger client module unit
	//if (appData->StudioMode())
	//	debugClient->RemoveModule(GetDocPath());

	return ibValueMetaObject::OnAfterCloseMetaObject();
}

// ⭐ WHAT WAS BUILT FROM THIS TEXT IS NOW STALE, and in the studio that is not a detail: a
// module's exports are READ FROM THE TEXT there (ibRuntimeModuleDataObject::ExportMethodsToHelper
// — nothing compiles a module in the studio, so there is no bytecode to read them from), and a
// name surface is built once and then cached. Nobody would notice the missing word until an export
// added five minutes ago failed to appear after the dot for the rest of the session.
//
// Said through the verb that already means it: InvalidateCompileModule is what the form editor
// calls on a form-edit commit (metaFormObject.cpp). It is a no-op for a module nothing has asked
// about yet, and it never CONSTRUCTS anything — an entry that is only a rebuilder stays one.
void ibValueMetaObjectModuleBase::InvalidateBuiltFromText()
{
	if (ibCompileValueCache* compileCache = m_metaData != nullptr ? m_metaData->GetCompileCache() : nullptr)
		compileCache->InvalidateCompileModule(this);
}

//***********************************************************************
//*                          default procedures						    *
//***********************************************************************

void ibValueMetaObjectModuleBase::SetDefaultProcedure(const ibString& procname, const ibContentHelper& contentHelper, std::vector<ibString> args)
{
	m_contentHelper.insert_or_assign(procname, CContentData{ contentHelper , args });
}

void ibValueMetaObjectModuleBase::SetDefaultFunction(const ibString& funcname, std::vector<ibString> args)
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

ibValueMetaObjectSharedModule::ibValueMetaObjectSharedModule(const ibString& name, const ibString& synonym, const ibString& comment) :
	ibValueMetaObjectModuleBase(name, synonym, comment)
{
}

bool ibValueMetaObjectSharedModule::ReadData(const ibDataNode& node)
{
	m_propertyModule->SetNodeValue(node.GetProperty(m_propertyModule->GetName()));
	m_propertyGlobalModule->SetNodeValue(node.GetProperty(m_propertyGlobalModule->GetName()));
	m_propertyPrivilegedModule->SetNodeValue(node.GetProperty(m_propertyPrivilegedModule->GetName()));
	return true;
}

bool ibValueMetaObjectSharedModule::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyModule->GetName(), m_propertyModule->GetNodeValue());
	node.SetProperty(m_propertyGlobalModule->GetName(), m_propertyGlobalModule->GetNodeValue());
	node.SetProperty(m_propertyPrivilegedModule->GetName(), m_propertyPrivilegedModule->GetNodeValue());
	return true;
}

//***********************************************************************
//*                          common value object                        *
//***********************************************************************

#include "backend/metaData.h"

bool ibValueMetaObjectSharedModule::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	return ibValueMetaObjectModuleBase::OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectSharedModule::OnLoadMetaObject(ibMetaData* metaData)
{
	return ibValueMetaObjectModuleBase::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectSharedModule::OnSaveMetaObject(int flags)
{
	return ibValueMetaObjectModuleBase::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectSharedModule::OnDeleteMetaObject()
{
	return ibValueMetaObjectModuleBase::OnDeleteMetaObject();
}

bool ibValueMetaObjectSharedModule::OnRenameMetaObject(const ibString& newName)
{
	// Runtime registry (metadata storage) — read by per-session managers.
	if (auto* storage = m_metaData->GetModuleStorage()) {
		if (!storage->RenameSharedModule(this, newName))
			return false;
	}

	// Studio registry — the editor's own compiled-unit holder.
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (auto* mgr = cc->GetModuleManager())
			mgr->RenameSharedModule(this, newName);
	}

	return ibValueMetaObjectModuleBase::OnRenameMetaObject(newName);
}

bool ibValueMetaObjectSharedModule::OnBeforeRunMetaObject(int flags)
{
	if (auto* storage = m_metaData->GetModuleStorage()) {
		if (!storage->AddSharedModule(this))
			return false;
	}

	// Studio registry: register a compiled lightweight unit. newObjectFlag = a
	// module just created in the studio → compile now; bulk load defers to
	// the manager's CreateMainModule.
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (auto* mgr = cc->GetModuleManager())
			if (!mgr->AddSharedModule(this, /*managerModule=*/false, (flags & newObjectFlag) != 0))
				return false;
	}

	return ibValueMetaObjectModuleBase::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectSharedModule::OnAfterRunMetaObject(int flags)
{
	return ibValueMetaObjectModuleBase::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectSharedModule::OnBeforeCloseMetaObject()
{
	if (auto* storage = m_metaData->GetModuleStorage()) {
		if (!storage->RemoveSharedModule(this))
			return false;
	}

	if (auto* cc = m_metaData->GetCompileCache()) {
		if (auto* mgr = cc->GetModuleManager())
			mgr->RemoveSharedModule(this);
	}

	// Was OnAfterCloseMetaObject — a long-standing copy/paste that fired the
	// after-hook from the before phase (and skipped the real before-hook).
	// ibValueMetaObjectManagerModule (the sibling) already does this correctly.
	return ibValueMetaObjectModuleBase::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectSharedModule::OnAfterCloseMetaObject()
{
	return ibValueMetaObjectModuleBase::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                          manager value object                       *
//***********************************************************************

bool ibValueMetaObjectManagerModule::OnBeforeRunMetaObject(int flags)
{
	if (auto* storage = m_metaData->GetModuleStorage()) {
		if (!storage->AddSharedModule(this))
			return false;
	}

	// Studio registry — a manager module registers as managerModule=true.
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (auto* mgr = cc->GetModuleManager())
			if (!mgr->AddSharedModule(this, /*managerModule=*/true, (flags & newObjectFlag) != 0))
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
		if (!storage->RemoveSharedModule(this))
			return false;
	}

	if (auto* cc = m_metaData->GetCompileCache()) {
		if (auto* mgr = cc->GetModuleManager())
			mgr->RemoveSharedModule(this);
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

METADATA_TYPE_REGISTER(ibValueMetaObjectSharedModule, "SharedModule", g_metaSharedModuleCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectManagerModule, "ManagerModule", g_metaManagerCLSID);
