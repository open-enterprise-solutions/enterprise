#ifndef __METAOBJECT_METADATA_H__
#define __METAOBJECT_METADATA_H__

#include "metaObject.h"
#include "metaObjectMetadataEnum.h"

#include "metaModuleObject.h"
#include "core/description/homePageDescription.h"   // ibHomePageDescription — the start-page workspace

//*****************************************************************************************
//*                                  metaData object                                      *
//*****************************************************************************************

// A NAME, not a caption: the root's name and its module's are identifiers, the same in every language - while it
// went through _(), a fresh base made under a Russian locale was named in Russian. A caption is the synonym's.
#define applicationDefaultName ibT("Application")

///////////////////////////////////////////////////////////////////////////

class BACKEND_API ibValueMetaObjectApplication : public ibValueMetaObject {
	public:

	enum
	{
		ID_METATREE_EDIT_HOME_PAGE,
	};

public:

#pragma region access
	bool AccessRight_Administration(const ibRoleUserInfo& roleInfo = ibRoleUserInfo()) const { return AccessRight(m_roleAdministration, roleInfo.IsSetRole() ? roleInfo : GetUserRoleInfo()); }
	bool AccessRight_DataAdministration(const ibRoleUserInfo& roleInfo = ibRoleUserInfo()) const { return AccessRight(m_roleDataAdministration, roleInfo.IsSetRole() ? roleInfo : GetUserRoleInfo()); }
	bool AccessRight_UpdateDatabase(const ibRoleUserInfo& roleInfo = ibRoleUserInfo()) const { return AccessRight(m_roleUpdateDatabase, roleInfo.IsSetRole() ? roleInfo : GetUserRoleInfo()); }
	bool AccessRight_ActiveUsers(const ibRoleUserInfo& roleInfo = ibRoleUserInfo()) const { return AccessRight(m_roleActiveUsers, roleInfo.IsSetRole() ? roleInfo : GetUserRoleInfo()); }
	bool AccessRight_SingleUserMode(const ibRoleUserInfo& roleInfo = ibRoleUserInfo()) const { return AccessRight(m_roleSingleUserMode, roleInfo.IsSetRole() ? roleInfo : GetUserRoleInfo()); }
	bool AccessRight_ModeAllFunction(const ibRoleUserInfo& roleInfo = ibRoleUserInfo()) const { return AccessRight(m_roleModeAllFunction, roleInfo.IsSetRole() ? roleInfo : GetUserRoleInfo()); }
	bool AccessRight_Assistant(const ibRoleUserInfo& roleInfo = ibRoleUserInfo()) const { return AccessRight(m_roleAssistant, roleInfo.IsSetRole() ? roleInfo : GetUserRoleInfo()); }
	bool AccessRight_DataAssistant(const ibRoleUserInfo& roleInfo = ibRoleUserInfo()) const { return AccessRight(m_roleDataAssistant, roleInfo.IsSetRole() ? roleInfo : GetUserRoleInfo()); }
#pragma endregion

	virtual ibClassID ResolveChild(const ibClassID& clsid) const {
		if (
			clsid == g_metaSharedModuleCLSID ||
			clsid == g_metaSharedFormCLSID ||
			clsid == g_metaSharedTemplateCLSID ||
			clsid == g_metaRoleCLSID ||
			clsid == g_metaSectionCLSID ||
			clsid == g_metaSharedCommandCLSID ||
			clsid == g_metaCommandGroupCLSID ||
			clsid == g_metaScheduledJobCLSID ||
			clsid == g_metaEventHandlerCLSID ||
			clsid == g_metaHttpServiceCLSID ||
			clsid == g_metaSessionParameterCLSID ||
			clsid == g_metaFeatureOptionCLSID ||
			clsid == g_metaSharedAttributeCLSID ||
			clsid == g_metaPictureCLSID ||
			clsid == g_metaLanguageCLSID ||
			clsid == g_metaConstantCLSID ||
			clsid == g_metaCatalogCLSID ||
			clsid == g_metaDocumentCLSID ||
			clsid == g_metaEnumerationCLSID ||
			clsid == g_metaDataToolCLSID ||
			clsid == g_metaReportCLSID ||
			clsid == g_metaInformationRegisterCLSID ||
			clsid == g_metaAccumulationRegisterCLSID ||
			clsid == g_metaParameterizedJobCLSID ||
			clsid == g_metaChartOfCharacteristicTypesCLSID ||
			clsid == g_metaChartOfAccountsCLSID ||
			clsid == g_metaAccountingRegisterCLSID ||
			clsid == g_metaChartOfCalculationTypesCLSID ||
			clsid == g_metaCalculationRegisterCLSID ||
			clsid == g_metaSequenceCLSID ||
			clsid == g_metaDocumentJournalCLSID
			)
			return clsid;

		return 0;
	}

	ibProgramSyntax GetCompileSyntax() const { return m_propertySyntax->GetValueAsEnum(); }

	// As a number: a version read from a file may be no rung of the enumeration, and casting it to one is undefined.
	void SetVersion(const ibVersionID& version) { m_propertyVersion->SetValue(static_cast<int>(version)); }
	ibVersionID GetVersion() const { return m_propertyVersion->GetValueAsInteger(); }

	void SetLanguage(const ibMetaID& id) { m_propertyDefLanguage->SetValue(id); }
	ibMetaID GetLanguage() const { return m_propertyDefLanguage->GetValueAsInteger(); }

	// The start-page workspace — the forms every session opens FIRST, and how they are split.
	// Read by the runtime composite (frontend ibHomePageDocument), written by the studio's
	// workspace editor. It is plain state on the root, not a property: the inspector has no
	// cell shape for "two ordered lists of forms", the dedicated editor has.
	const ibHomePageDescription& GetHomePage() const { return m_homePage; }
	ibHomePageDescription& GetHomePage() { return m_homePage; }
	void SetHomePage(const ibHomePageDescription& homePage) { m_homePage = homePage; }

	//////////////////////////////////////////////////////////////////////////////////////////////

	ibString GetLangCode() const;

	//////////////////////////////////////////////////////////////////////////////////////////////

	ibValueMetaObjectApplication();
	virtual ~ibValueMetaObjectApplication();

	virtual ibString GetFullName() const { return applicationDefaultName; }
	virtual ibString GetModuleName() const { return applicationDefaultName; }

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	//events
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	/**
	* Property events
	*/
	virtual void OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue);

	//prepare menu for item
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);
	virtual void ProcessCommand(unsigned int id);

	//create function 
	static bool ExecuteSystemSQLCommand();

public:

	virtual const ibValueMetaObjectModule* GetObjectModule() const { return m_propertyModuleApplication->GetMetaObject(); }

	// The session module — where SetSessionParameters lives. Null-safe by the same
	// rule as the application module: an application that declares no session
	// parameters simply has an empty one.
	const class ibValueMetaObjectManagerModule* GetSessionModule() const { return m_propertyModuleSession->GetMetaObject(); }

protected:

	//load & save metaData from DB

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	// Shared body for the property-list fill callbacks below — they differ only
	// by the metaobject CLSID they collect. The thin FillRoleList/FillLanguageList
	// delegates are kept because they're referenced by member-function-pointer in
	// the ibPropertyList property declarations.
	bool FillListByClsid(ibPropertyList* prop, const ibClassID& clsid) {
		std::vector<ibValueMetaObject*> array;
		if (FillArrayObjectByFilter(array, { clsid })) {
			for (const auto child : array) {
				prop->AppendItem(
					child->GetName(),
					child->GetMetaID(),
					child->GetClassType(),
					child);
			}
			return true;
		}
		return false;
	}

	bool FillRoleList(ibPropertyList* prop)     { return FillListByClsid(prop, g_metaRoleCLSID); }
	bool FillLanguageList(ibPropertyList* prop) { return FillListByClsid(prop, g_metaLanguageCLSID); }

	ibHomePageDescription m_homePage;

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyModuleApplication = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, ibT("ApplicationModule"), _("Application module"), _("The interactive client's application module: BeforeStart (may refuse the login), OnStart (opens the desktop), BeforeExit / OnExit, and its exported procedures and variables, visible to every form. Not run for background jobs."));

	// THE SESSION MODULE — a second module on the root, and the only place a session
	// parameter may be written. It carries one procedure, SetSessionParameters, run
	// once per session before anything reads data: the values it sets are what row
	// access is filtered by, so they have to exist before the first query and stay
	// unchanged after it.
	//
	// Separate from the application module rather than another handler inside it,
	// because the two run at different moments and for different audiences. The
	// application module speaks to an interactive client — BeforeStart can refuse
	// a login, OnStart opens the desktop — and never runs for a background job. This
	// one runs for EVERY session, job included, and runs earlier.
	// A MANAGER module, not a plain one — and that is not a style choice. A plain
	// ibValueMetaObjectModule never registers itself with the module storage, so it
	// is never compiled into a session and its procedure can never be called: the
	// module would exist in the tree, open in the editor, and quietly do nothing.
	// A manager module registers (AddSharedModule) and is compiled once with the
	// session's modules, which is exactly what a scheduled job's handler relies on.
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyModuleSession = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, ibT("SessionModule"), _("Session module"), _("Runs SetSessionParameters once per session - interactive or background - before anything reads data. The only place a session parameter may be written; row access is filtered by the values it sets, so they exist before the first query and do not change after it."));

	ibPropertyCategory* m_propertyPresetValues = ibPropertyObject::CreatePropertyCategory(ibT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefRole = ibPropertyObject::CreateProperty<ibPropertyList>(m_propertyPresetValues, ibT("DefaultRole"), _("Default role"), _("The role meant for users who have none of their own. Saved with the application, but no session reads it yet: a user without roles gets each right's own default."), &ibValueMetaObjectApplication::FillRoleList);
	ibPropertyList* m_propertyDefLanguage = ibPropertyObject::CreateProperty<ibPropertyList>(m_propertyPresetValues, ibT("DefaultLanguage"), _("Default language"), _("The application's own language: captions written without naming a language go into it, and a user with no language of their own sees it."), &ibValueMetaObjectApplication::FillLanguageList);

	ibPropertyCategory* m_compatibilityCategory = ibPropertyObject::CreatePropertyCategory(ibT("Compatibility"), _("Compatibility"));
	ibPropertyEnum<ibValueEnumVersion>* m_propertyVersion = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumVersion>>(m_compatibilityCategory, ibT("Version"), _("Version"), _("The compatibility version: which platform behaviour the application asks for. Don't use compatibility (the default) runs the current behaviour. Saved with the application and reported by the tools; no behaviour is switched by it yet."), version_oes_last);
	// CES is the default for new applications. VES (Visual Basic-style
	// ES, a legacy business-scripting dialect) is kept available for legacy / migrated
	// applications and acts as a "please migrate" signal in the
	// metadata UI.
	ibPropertyEnum<ibValueEnumSyntax>* m_propertySyntax = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSyntax>>(m_compatibilityCategory, ibT("Syntax"), _("Syntax"), _("The dialect the application's modules are written in. CES (the default for new applications): braces and semicolons. VES: the legacy word-based dialect, kept for migrated applications."), syntax_ces);

#pragma region role 
	ibRole* m_roleAdministration = ibValueMetaObject::CreateRole(ibT("Administration"), _("Administration"));
	ibRole* m_roleDataAdministration = ibValueMetaObject::CreateRole(ibT("DataAdministration"), _("Data administration"));
	ibRole* m_roleUpdateDatabase = ibValueMetaObject::CreateRole(ibT("UpdateDatabase"), _("Update database"));
	ibRole* m_roleActiveUsers = ibValueMetaObject::CreateRole(ibT("ActiveUsers"), _("Active users"));
	ibRole* m_roleSingleUserMode = ibValueMetaObject::CreateRole(ibT("SingleUserMode"), _("Single-user mode"));
	ibRole* m_roleModeAllFunction = ibValueMetaObject::CreateRole(ibT("ModeAllFunctions"), _("Mode \"All functions\""));
	ibRole* m_roleAssistant = ibValueMetaObject::CreateRole(ibT("Assistant"), _("Assistant"), false);
	ibRole* m_roleDataAssistant = ibValueMetaObject::CreateRole(ibT("DataAssistant"), _("Data assistant"), false);
#pragma endregion
};

#endif 