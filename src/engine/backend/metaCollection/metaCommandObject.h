#ifndef __META_COMMAND_OBJECT_H__
#define __META_COMMAND_OBJECT_H__

#include "metaModuleObject.h"   // ibPropertyInnerModule + ibValueMetaObjectModule (the handler module)
#include "metaFormObject.h"     // ibBackendCommandItem (the command mixin)
#include "backend/backend_command.h"   // ibBackendCommandSender — a command is command-capable: it hops WITHIN itself (sub-commands)
#include "backend/moduleInfo.h" // ibRuntimeModuleDataObject (the command's private runtime descriptor)
#include "backend/propertyManager/property/propertyType.h"   // ibPropertyType — the command's parameter data type
#include "backend/compiler/enumUnit.h"                       // ibValueEnumeration — the interface-area enum value class
#include "backend/propertyManager/property/propertyEnum.h"   // ibPropertyEnum — the area dropdown (declare once, all surfaces)

// The interface-area enum as a runtime value — one declaration gives the script value, the inspector dropdown and
// a serialisable property (see docs/enumerations.md). Members mirror ibInterfaceCommandSection (Combined excluded —
// it is a list object's own dual mode, not a place a command is put).
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

	// The interface AREA this command occupies once it is included in a section (Important / Normal / Create /
	// Reports / Service) — property-driven, NOT hardcoded by type: a command is a free citizen, the developer
	// places it. A section's menu builder reads this (GetInterfaceItemArrayObject) to lay the command in the right
	// panel; the form's command picker sees the SAME sections the same way.
	virtual ibInterfaceCommandSection GetCommandSection() const override {
		return m_propertyInterfaceArea->GetValueAsEnum();
	}


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
	// data of this type (empty = not typed, available everywhere). 1C's "command parameter type".
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
	virtual bool PrepareContextMenu(wxMenu* defaultMenu) override;
	virtual void ProcessCommand(unsigned int id) override;

	// ibBackendCommandItem — a command OVERRIDES the global call: run the handler, not open a form.
	virtual bool Execute(ibInterfaceCommandType cmdType, ibBackendValueForm* srcForm, ibValue* commandParameter) const override;

	//lifecycle — forward to the inner module (its own load/save/designer registration; NOT common-module)
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

protected:

	enum { ID_METATREE_OPEN_COMMAND_MODULE = 19100 };

	// required by ibBackendCommandItem, but a command overrides Execute and never opens a form.
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType /*cmdType*/ = ibInterfaceCommandType::ibInterfaceCommandType_Default) const override { return nullptr; }

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryCommand      = ibPropertyObject::CreatePropertyCategory(wxT("Command"), _("Command"));
	// Interface area — where the command sits once included in a section; default Normal. GetCommandSection reads it.
	ibPropertyEnum<ibValueEnumInterfaceCommandSection>* m_propertyInterfaceArea = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumInterfaceCommandSection>>(m_categoryCommand, wxT("InterfaceArea"), _("Interface area"), ibInterfaceCommandSection_Default);
	ibPropertyPicture*  m_propertyPicture      = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryCommand, wxT("Picture"), _("Picture"));
	ibPropertyTString*  m_propertyTooltip      = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryCommand, wxT("Tooltip"), _("Tooltip"), wxEmptyString);
	ibPropertyBoolean*  m_propertyModifiesData = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryCommand, wxT("ModifiesData"), _("Modifies data"), true);
	// Default TYPE_STRING (a VALID variant, like an attribute's Type) — a primitive is not a reference, so an
	// untouched command reads as NOT parameterized (shown everywhere). TYPE_EMPTY builds a broken type variant
	// whose GetValueAsTypeDesc dereferences garbage — never default a type property to it.
	ibPropertyType*     m_propertyParamType    = ibPropertyObject::CreateProperty<ibPropertyType>(m_categoryCommand, wxT("ParameterType"), _("Parameter type"), ibValueTypes::TYPE_STRING);

	// inner handler module (PLAIN module, like Constant's record module) — carries CommandProcessing,
	// serialises itself; compiled on invocation by ibValueCommandDataObject, never a common module.
	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyCommandModule =
		ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(
			m_categoryContext, wxT("CommandModule"), _("Command module"));

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
