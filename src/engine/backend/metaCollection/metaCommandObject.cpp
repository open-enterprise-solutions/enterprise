#include "metaCommandObject.h"

#include "backend/metaData.h"                       // ibMetaData::GetAnyArrayObject
#include "backend/session/session.h"                // ibSession::EditModuleManagerFor
#include "backend/moduleManager/moduleManager.h"    // ibValueModuleManager (descriptor scope parent)
#include "backend/backend_form.h"                   // ibBackendValueForm + formWrapper::cast_value
#include "backend/appData.h"                         // appData->DesignerMode()

//***********************************************************************
//*                            Command object                          *
//***********************************************************************

ibValueMetaObjectCommand::ibValueMetaObjectCommand(const wxString& name, const wxString& synonym, const wxString& comment)
	: ibValueMetaObject(name, synonym, comment)
{
	// One handler shape for object and general commands (docs/command-arc.md §6):
	//   Procedure CommandProcessing(CommandParameter, ExecuteParameters)
	(*m_propertyCommandModule)->SetDefaultProcedure(wxT("CommandProcessing"), ibContentHelper::eProcedureHelper,
		{ wxT("CommandParameter"), wxT("ExecuteParameters") });
}

//***********************************************************************
//*        lifecycle — forward to the inner module (own load/save)      *
//***********************************************************************

bool ibValueMetaObjectCommand::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObject::OnCreateMetaObject(metaData, flags))
		return false;
	return (*m_propertyCommandModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectCommand::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyCommandModule)->OnLoadMetaObject(metaData))
		return false;
	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectCommand::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyCommandModule)->OnSaveMetaObject(flags))
		return false;
	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectCommand::OnDeleteMetaObject()
{
	if (!(*m_propertyCommandModule)->OnDeleteMetaObject())
		return false;
	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectCommand::OnBeforeRunMetaObject(int flags)
{
	// forwards to the inner module so the designer code editor sees it; a PLAIN module does NOT
	// register with the module manager (that is only CommonModule / ManagerModule).
	if (!(*m_propertyCommandModule)->OnBeforeRunMetaObject(flags))
		return false;
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectCommand::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyCommandModule)->OnBeforeCloseMetaObject())
		return false;
	return ibValueMetaObject::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectCommand::OnAfterCloseMetaObject()
{
	if (!(*m_propertyCommandModule)->OnAfterCloseMetaObject())
		return false;
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*     the global call, OVERRIDDEN: a command RUNS its handler         *
//***********************************************************************

bool ibValueMetaObjectCommand::Execute(ibInterfaceCommandType /*cmdType*/, ibBackendValueForm* srcForm, ibValue* commandParameter) const
{
	// Run the handler through the command's OWN descriptor — private, no common-module registration,
	// no name-resolvable exports (a command handler is never called by name from script).
	// Direct-initialised: ibValuePtr's pointer constructor is explicit, so `= new …` is
	// copy-initialisation and ill-formed. MSVC accepts it anyway; GCC does not.
	ibValuePtr<ibValueCommandDataObject> runtime(new ibValueCommandDataObject(this));

	//   CommandParameter = what the command runs on (caller-supplied); ExecuteParameters = the source form.
	ibValue cmdParam  = commandParameter ? *commandParameter : ibValue();
	ibValue execParam = srcForm ? ibValue(formWrapper::inl::cast_value(srcForm)) : ibValue();
	return runtime->RunHandler(cmdParam, execParam);
}

// ibBackendCommandSender — a GROUP command hops INTO its sub-commands (a section resolves its own inner ids).
// The returned value is itself command-capable, so ResolveCommandPath keeps climbing. A plain leaf has no sub and
// the walk terminates on THIS command (its leaf value is run / read by the caller). Mirror of a source hop.
bool ibValueMetaObjectCommand::GetCommandByHop(const ibCommandHop& hop, ibValue& out)
{
	for (ibValueMetaObjectCommand* sub : GetSubCommands())
		if (sub != nullptr && sub->CompareId(hop.m_id)) {
			out = static_cast<const ibValue*>(sub);   // const-ref to the sub-command — itself command-capable
			return true;
		}
	return false;
}

//***********************************************************************
//*                          load & save from DB                        *
//***********************************************************************

bool ibValueMetaObjectCommand::ReadData(const ibDataNode& node)
{
	if (!ibValueMetaObject::ReadData(node))
		return false;

	m_propertyCommandModule->ReadNodeValue(node.GetProperty(m_propertyCommandModule->GetName()));
	m_propertyInterfaceArea->ReadNodeValue(node.GetProperty(m_propertyInterfaceArea->GetName()));
	m_propertyPicture->ReadNodeValue(node.GetProperty(m_propertyPicture->GetName()));
	m_propertyTooltip->ReadNodeValue(node.GetProperty(m_propertyTooltip->GetName()));
	m_propertyModifiesData->ReadNodeValue(node.GetProperty(m_propertyModifiesData->GetName()));
	m_propertyParamType->ReadNodeValue(node.GetProperty(m_propertyParamType->GetName()));
	return true;
}

bool ibValueMetaObjectCommand::WriteData(ibDataNode& node) const
{
	if (!ibValueMetaObject::WriteData(node))
		return false;

	node.SetProperty(m_propertyCommandModule->GetName(),   m_propertyCommandModule->GetNodeValue());
	node.SetProperty(m_propertyInterfaceArea->GetName(),   m_propertyInterfaceArea->GetNodeValue());
	node.SetProperty(m_propertyPicture->GetName(),         m_propertyPicture->GetNodeValue());
	node.SetProperty(m_propertyTooltip->GetName(),         m_propertyTooltip->GetNodeValue());
	node.SetProperty(m_propertyModifiesData->GetName(),    m_propertyModifiesData->GetNodeValue());
	node.SetProperty(m_propertyParamType->GetName(),       m_propertyParamType->GetNodeValue());
	return true;
}

//***********************************************************************
//*      Command runtime — a private descriptor (mirrors Constant)      *
//***********************************************************************

ibValueCommandDataObject::ibValueCommandDataObject(const ibValueMetaObjectCommand* metaObject)
	: ibValueDynamicMembers(ibValueTypes::TYPE_EMPTY), ibRuntimeModuleDataObject(m_members, this),
	  m_metaObject(metaObject)
{
	// Wire the descriptor to the running module scope, compile the command's module, run its top-level
	// — the SAME sequence a constant / record object uses to run its own module (constantObject.cpp).
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(m_metaObject->GetMetaData());
	ibRuntimeModuleDataObject::SetParent(moduleManager);

	InitializeRuntime();
	try {
		Compile();
	}
	catch (const ibBackendException&) {
		if (!appData->DesignerMode())
			throw;
		return;
	}
	Run(true);
}

const ibValueMetaObjectModuleBase* ibValueCommandDataObject::GetMetaForCompile() const
{
	return m_metaObject ? m_metaObject->GetCommandModule() : nullptr;
}

bool ibValueCommandDataObject::RunHandler(ibValue& commandParameter, ibValue& executeParameters)
{
	// idiomatic fixed-N handler call (mirrors ExecAsProc(wxT("BeforeWrite"), cancel)):
	// the variadic packs the two ibValue& into the ProcUnit's ibValue*[] form itself.
	ExecAsProc(wxT("CommandProcessing"), commandParameter, executeParameters);
	return true;
}

//***********************************************************************
//*                        Register in runtime                          *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectCommand,       "Command",       g_metaCommandCLSID);          // under an object (plain, like Form)
METADATA_TYPE_REGISTER(ibValueMetaObjectCommonCommand, "CommonCommand", g_metaCommonCommandCLSID);   // config-level (like CommonForm)

// the command interface-area enum — one line makes it a script value, a dropdown and a serialisable property
ENUM_TYPE_REGISTER(ibValueEnumInterfaceCommandSection, "CommandInterfaceArea", enum_to_clsid("EN_CIAR"));
