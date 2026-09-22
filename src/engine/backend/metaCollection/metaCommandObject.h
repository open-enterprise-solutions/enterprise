#ifndef __META_COMMAND_OBJECT_H__
#define __META_COMMAND_OBJECT_H__

#include "metaModuleObject.h"   // ibPropertyInnerModule + ibValueMetaObjectModule (the handler module)
#include "metaFormObject.h"     // ibBackendCommandItem (the command mixin)
#include "backend/backend_command.h"   // ibBackendCommandSender — a command is command-capable: it hops WITHIN itself (sub-commands)
#include "backend/moduleInfo.h" // ibRuntimeModuleDataObject (the command's private runtime descriptor)
#include "backend/propertyManager/property/propertyType.h"   // ibPropertyType — the command's parameter data type
#include "backend/compiler/enumUnit.h"                       // ibValueEnumeration — the interface-area enum value class

// The interface-area enum as a runtime value — the script value, and the captions of the platform's own command
// groups (ibCommandGroupCaption reads them here, so a group is named in one place). Members mirror
// ibInterfaceCommandSection (Combined excluded — it is a list object's own dual mode, not a place a command is put).
class ibValueEnumInterfaceCommandSection : public ibValueEnumeration<ibInterfaceCommandSection> {
public:
	ibValueEnumInterfaceCommandSection() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibInterfaceCommandSection_Important, wxT("Important"), _("Important"));
		AddEnumeration(ibInterfaceCommandSection_Default,   wxT("Normal"),    _("Normal"));
		AddEnumeration(ibInterfaceCommandSection_Create,    wxT("Create"),    _("Create"));
		AddEnumeration(ibInterfaceCommandSection_Report,    wxT("Reports"),   _("Reports"));
		AddEnumeration(ibInterfaceCommandSection_Service,   wxT("Service"),   _("Service"));
	}
};

//********************************************************************************************
//*                              Command — the command metaobject                           *
//********************************************************************************************

// A COMMAND metaobject. Like a Constant, it is a plain ibValueMetaObject that HOLDS an inner
// module carrying the handler code. Unlike a common/manager module, the command's module is NOT
// registered with the module manager and is NOT reachable by name from script — a command's
// handler is only ever invoked BY the command. As an ibBackendCommandItem it OVERRIDES the global
// Execute: it runs its handler
//   Procedure CommandProcessing(CommandParameter, ExecuteParameters)
// through its OWN runtime descriptor (ibValueCommandDataObject below), exactly the way a record /
// constant object runs its module — private, no external access. Common vs object scope = the METATYPE
// (ibValueMetaObjectCommonCommand vs an object command under its owner), not a property.
class BACKEND_API ibValueMetaObjectCommand : public ibValueMetaObject, public ibBackendCommandItem, public ibBackendTypeConfigFactory, public ibBackendCommandSender {
public:

	ibValueMetaObjectCommand(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	bool AccessRight_Use() const { return IsFullAccess() || AccessRight(m_roleUse); }

	// ibBackendCommandSender — a GROUP command hops INTO its sub-commands (a section resolves its own inner ids),
	// so the command path climbs uniformly. A plain leaf command has none and the walk terminates on it.
	virtual bool GetCommandByHop(const ibCommandHop& hop, ibValue& out) override;

	// ⭐ WHERE THE COMMAND IS FILED once a section includes it — its GROUP, one property, one number: a platform
	// group (Important / Normal / Create / Reports / Service) or a group the configuration declares. Property-driven,
	// NOT hardcoded by type: a command is a free citizen, the developer places it. A section's page reads it through
	// GetInterfaceItemArrayObject; the form's command picker sees the SAME sections the same way.
	//
	// The platform area, when the command sits in a platform group. A command filed under a declared group sits in
	// none of them — the section files it under that group — and answers Normal here only because the contract has
	// no "none".
	virtual ibInterfaceCommandSection GetCommandSection() const override;
	// …and the declared group, when that is where the command sits; null in a platform group, and null again when
	// the group it named has been deleted (the command falls back to Normal rather than out of every page).
	const class ibValueMetaObjectCommandGroup* GetCommandGroup() const;


	// projected buttons inherit these LIVE from the command (not edited on the button)
	wxBitmap GetPictureAsBitmap() const {
		if (!m_propertyPicture->IsEmptyProperty()) return m_propertyPicture->GetValueAsBitmap();
		return ibBackendPicture::CreatePicture(g_metaCommonMetadataCLSID);
	}
	// The command carries NO picture -> a projection of it is TEXT-ONLY (do not force the metatype glyph). A picture
	// is inherited from the command only when the command actually HAS one; a table action bakes its own picture.
	bool IsEmptyPicture() const { return m_propertyPicture->IsEmptyProperty(); }
	wxString GetToolTip()      const { return m_propertyTooltip->GetValueAsTranslateString(); }
	bool     GetModifiesData() const { return m_propertyModifiesData->GetValueAsBoolean(); }
	// Parameter data type — a "parameterizable" command shows in a form's GLOBAL section only where the form has
	// data of this type (empty = not typed, available everywhere) — the command's parameter type.
	const ibTypeDescription& GetParameterType() const { return m_propertyParamType->GetValueAsTypeDesc(); }

	// HUB — a command may HOLD commands (recursion, exactly as a subsystem holds subsystems). It therefore accepts
	// child commands; a command WITH children is a GROUP whose projection is a dropdown menu (its click opens the
	// submenu of sub-commands), not a terminal run. A leaf command (no children) runs directly.
	virtual ibClassID ResolveChild(const ibClassID& clsid) const override {
		return (clsid == g_metaCommonCommandCLSID || clsid == g_metaCommandCLSID) ? clsid : 0;
	}
	std::vector<ibValueMetaObjectCommand*> GetSubCommands(
		std::vector<ibValueMetaObjectCommand*> array = std::vector<ibValueMetaObjectCommand*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectCommand>(array, { g_metaCommonCommandCLSID, g_metaCommandCLSID });
		return array;
	}
	// True if any live child is a command — a direct scan, no array allocation (this runs per node while the
	// designer tree / a dropdown is built).
	bool HasSubCommands() const {
		for (unsigned int i = 0; i < GetChildCount(); i++)
			if (const ibValueMetaObject* child = GetChild(i))
				if (child->IsAllowed() && (child->GetClassType() == g_metaCommonCommandCLSID || child->GetClassType() == g_metaCommandCLSID))
					return true;
		return false;
	}

	// --- ibBackendTypeConfigFactory — the ibPropertyType (Parameter type) variant resolves through the owning
	// factory (mirror the value-table column / attribute). GetTypeDesc returns the parameter type's OWN storage
	// (reads the variant's typedesc — no recursion); the filter offers reference types; GetMetaData is the
	// command's config. Re-declared (const + non-const) to CLOSE the pure GetMetaData across the two bases. ---
	virtual ibTypeDescription& GetTypeDesc() const override { return m_propertyParamType->GetValueAsTypeDesc(); }
	virtual ibSelectorDataType GetFilterDataType() const override { return ibSelectorDataType::ibSelectorDataType_reference; }
	virtual const ibMetaData* GetMetaData() const override { return m_metaData; }
	virtual ibMetaData* GetMetaData() override { return m_metaData; }

	// the command's handler module (holds CommandProcessing) — compiled by its runtime descriptor
	const ibValueMetaObjectModule* GetCommandModule() const { return m_propertyCommandModule->GetMetaObject(); }

	// tree icon — the "run command" glyph (base64 PNG in metaCommandObject_res.cpp, like every metatype).
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	// designer context menu — "Open command module" opens the handler module's code editor (metaCommandObjectMenu.cpp),
	// exactly as a common module / object opens its module.
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items) override;

	// ibBackendCommandItem — a command OVERRIDES the global call: run the handler, not open a form.
	virtual bool Execute(ibInterfaceCommandType cmdType, ibBackendValueForm* srcForm, ibValue* commandParameter) const override;

	//lifecycle — forward to the inner module (its own load/save/designer registration; NOT common-module)
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();
	virtual bool OnBeforeRunMetaObject(int flags);
	// ⭐ WHERE THE COMMAND'S MODULE BECOMES CHECKABLE — see the .cpp. Every other
	// metatype registers its module with the designer's compile cache here; a
	// command did not, so nothing could compile it.
	virtual bool OnAfterRunMetaObject(int flags);
	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

protected:


	// required by ibBackendCommandItem, but a command overrides Execute and never opens a form.
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType /*cmdType*/ = ibInterfaceCommandType::ibInterfaceCommandType_Default) const override { return nullptr; }

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	// The choices of Group: the platform's groups, then every group the configuration declares (the .cpp says why
	// one number can stand for both).
	bool FillGroupList(ibPropertyList* prop);

	ibPropertyCategory* m_categoryCommand      = ibPropertyObject::CreatePropertyCategory(wxT("Command"), _("Command"));
	// Group — where the command sits once included in a section; default Normal. GetCommandSection / GetCommandGroup
	// read it. Saved as "Group"; a configuration written before it has "InterfaceArea", the same number (ReadData).
	ibPropertyList* m_propertyGroup = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryCommand, wxT("Group"), _("Group"), _("Where the command is shown. A group of a section page - one of the platform's (Important, Normal, Create, Reports, Service) or a command group the configuration declares for a section - once a section includes the command; or a command group of the form command bar, which puts it in that group's submenu on the forms of its object: the owner's for an object's command, the one its Parameter type names for a common command. Normal by default."), &ibValueMetaObjectCommand::FillGroupList, ibInterfaceCommandSection_Default);
	ibPropertyPicture*  m_propertyPicture      = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryCommand, wxT("Picture"), _("Picture"), _("The icon shown beside the command's caption in a section, a command bar or a menu. Empty: caption only."));
	ibPropertyTString*  m_propertyTooltip      = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryCommand, wxT("Tooltip"), _("Tooltip"), _("The hint shown when the pointer rests on the command, one text per language. Empty: the synonym is shown."), wxEmptyString);
	ibPropertyBoolean*  m_propertyModifiesData = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryCommand, wxT("ModifiesData"), _("Modifies data"), _("Declares that running the command changes data, so it has no place where data is only viewed. Saved with the command; nothing hides or disables it by this yet."), true);
	// Default TYPE_STRING (a VALID variant, like an attribute's Type) — a primitive is not a reference, so an
	// untouched command reads as NOT parameterized (shown everywhere). TYPE_EMPTY builds a broken type variant
	// whose GetValueAsTypeDesc dereferences garbage — never default a type property to it.
	ibPropertyType*     m_propertyParamType    = ibPropertyObject::CreateProperty<ibPropertyType>(m_categoryCommand, wxT("ParameterType"), _("Parameter type"), _("What the command is run FOR. Naming one or more reference types makes it a typed command: it is offered only on forms carrying data of such a type, and CommandProcessing receives that reference as its parameter. A primitive type (the default) leaves it untyped - offered everywhere."), ibValueTypes::TYPE_STRING);

	// inner handler module (PLAIN module, like Constant's record module) — carries CommandProcessing,
	// serialises itself; compiled on invocation by ibValueCommandDataObject, never a common module.
	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyCommandModule =
		ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(
			m_categoryContext, wxT("CommandModule"), _("Command module"), _("The command's code: CommandProcessing(CommandParameter, ExecuteParameters) runs when the command is invoked, with the typed command's reference as the parameter. Compiled when the command is run."));

	ibRole* m_roleUse = ibValueMetaObject::CreateRole(wxT("Use"), _("Use"));

	friend class ibMetaData;
};

//********************************************************************************************
//*          Common command — a config-level command (own clsid, like CommonForm)            *
//********************************************************************************************

// The command-side twin of CommonForm vs Form: same behaviour as the object command (runs its handler through a
// transient runtime), but a DISTINCT clsid + name ("CommonCommand") so a config-wide command is told apart from an
// object's own command at a glance. ibValueMetaObjectCommand itself is the OBJECT command (plain name, like Form,
// lives under a business object); this common variant lives at the config root.
class BACKEND_API ibValueMetaObjectCommonCommand : public ibValueMetaObjectCommand {
public:
	ibValueMetaObjectCommonCommand(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString)
		: ibValueMetaObjectCommand(name, synonym, comment) {}

	// A general command checked into a section renders in that section's menu and runs
	// from there — which is the whole reason the section editor lists commands at all.
	virtual bool IsInterfaceAllowed() const override { return true; }
};

//********************************************************************************************
//*                  Command runtime — a private per-invocation descriptor                  *
//********************************************************************************************

// The command's own runtime, mirroring ibValueRecordDataObjectConstant: it IS a descriptor
// (ibRuntimeModuleDataObject), compiles the command's module, and runs CommandProcessing on its
// OWN ProcUnit — no module-manager registration, no name-resolvable exports. Built transiently by
// ibValueMetaObjectCommand::Execute for a single invocation.
class BACKEND_API ibValueCommandDataObject : public ibValueDynamicMembers, public ibRuntimeModuleDataObject {
public:

	ibValueCommandDataObject(const ibValueMetaObjectCommand* metaObject);

	// run the handler with its two real arguments (CommandParameter, ExecuteParameters)
	bool RunHandler(ibValue& commandParameter, ibValue& executeParameters);

	virtual bool IsEmpty() const override { return false; }

protected:

	// compile target — the command's own module
	virtual const ibValueMetaObjectModuleBase* GetMetaForCompile() const override;

private:

	const ibValueMetaObjectCommand* m_metaObject;
};

#endif
