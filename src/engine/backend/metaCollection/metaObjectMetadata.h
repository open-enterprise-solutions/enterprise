#ifndef __METAOBJECT_METADATA_H__
#define __METAOBJECT_METADATA_H__

#include "metaObject.h"
#include "metaObjectMetadataEnum.h"

#include "metaModuleObject.h"
#include "backend/homePageDescription.h"   // ibHomePageDescription — the start-page workspace

//*****************************************************************************************
//*                                  metaData object                                      *
//*****************************************************************************************

#define configurationDefaultName _("Configuration")

///////////////////////////////////////////////////////////////////////////

class BACKEND_API ibValueMetaObjectConfiguration : public ibValueMetaObject {
	public:

	enum
	{
		ID_METATREE_OPEN_INIT_MODULE = 19000,
		ID_METATREE_OPEN_SESSION_MODULE,
		ID_METATREE_EDIT_HOME_PAGE,
	};

public:

#pragma region access
	bool AccessRight_Administration(const ibRoleUserInfo& roleInfo = ibRoleUserInfo()) const { return AccessRight(m_roleAdministration, roleInfo.IsSetRole() ? roleInfo : GetUserRoleInfo()); }
	bool AccessRight_DataAdministration(const ibRoleUserInfo& roleInfo = ibRoleUserInfo()) const { return AccessRight(m_roleDataAdministration, roleInfo.IsSetRole() ? roleInfo : GetUserRoleInfo()); }
	bool AccessRight_UpdateDatabaseConfiguration(const ibRoleUserInfo& roleInfo = ibRoleUserInfo()) const { return AccessRight(m_roleUpdateDatabaseConfiguration, roleInfo.IsSetRole() ? roleInfo : GetUserRoleInfo()); }
	bool AccessRight_ActiveUsers(const ibRoleUserInfo& roleInfo = ibRoleUserInfo()) const { return AccessRight(m_roleActiveUsers, roleInfo.IsSetRole() ? roleInfo : GetUserRoleInfo()); }
	bool AccessRight_ExclusiveMode(const ibRoleUserInfo& roleInfo = ibRoleUserInfo()) const { return AccessRight(m_roleExclusiveMode, roleInfo.IsSetRole() ? roleInfo : GetUserRoleInfo()); }
	bool AccessRight_ModeAllFunction(const ibRoleUserInfo& roleInfo = ibRoleUserInfo()) const { return AccessRight(m_roleModeAllFunction, roleInfo.IsSetRole() ? roleInfo : GetUserRoleInfo()); }
#pragma endregion

	virtual ibClassID ResolveChild(const ibClassID& clsid) const {
		if (
			clsid == g_metaCommonModuleCLSID ||
			clsid == g_metaCommonFormCLSID ||
			clsid == g_metaCommonTemplateCLSID ||
			clsid == g_metaRoleCLSID ||
			clsid == g_metaSectionCLSID ||
			clsid == g_metaCommonCommandCLSID ||
			clsid == g_metaScheduledJobCLSID ||
			clsid == g_metaSessionParameterCLSID ||
			clsid == g_metaPictureCLSID ||
			clsid == g_metaLanguageCLSID ||
			clsid == g_metaConstantCLSID ||
			clsid == g_metaCatalogCLSID ||
			clsid == g_metaDocumentCLSID ||
			clsid == g_metaEnumerationCLSID ||
			clsid == g_metaDataProcessorCLSID ||
			clsid == g_metaReportCLSID ||
			clsid == g_metaInformationRegisterCLSID ||
			clsid == g_metaAccumulationRegisterCLSID ||
			clsid == g_metaParameterizedJobCLSID ||
			clsid == g_metaChartOfCharacteristicTypesCLSID ||
			clsid == g_metaChartOfAccountsCLSID ||
			clsid == g_metaAccountingRegisterCLSID
			)
			return clsid;

		return 0;
	}

	ibProgramSyntax GetCompileSyntax() const { return m_propertySyntax->GetValueAsEnum(); }

	void SetVersion(const ibVersionID& version) { m_propertyVersion->SetValue(static_cast<ibProgramVersion>(version)); }
	ibVersionID GetVersion() const { return m_propertyVersion->GetValueAsInteger(); }

	void SetLanguage(const ibMetaID& id) { m_propertyDefLanguage->SetValue(id); }
	ibMetaID GetLanguage() const { return m_propertyDefLanguage->GetValueAsInteger(); }

	// The start-page workspace — the forms every session opens FIRST, and how they are split.
	// Read by the runtime composite (frontend ibHomePageDocument), written by the designer's
	// workspace editor. It is plain state on the root, not a property: the inspector has no
	// cell shape for "two ordered lists of forms", the dedicated editor has.
	const ibHomePageDescription& GetHomePage() const { return m_homePage; }
	ibHomePageDescription& GetHomePage() { return m_homePage; }
	void SetHomePage(const ibHomePageDescription& homePage) { m_homePage = homePage; }

	//////////////////////////////////////////////////////////////////////////////////////////////

	wxString GetLangCode() const;

	//////////////////////////////////////////////////////////////////////////////////////////////

	ibValueMetaObjectConfiguration();
	virtual ~ibValueMetaObjectConfiguration();

	virtual wxString GetFullName() const { return configurationDefaultName; }
	virtual wxString GetModuleName() const { return configurationDefaultName; }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

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
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

	//create function 
	static bool ExecuteSystemSQLCommand();

public:

	virtual const ibValueMetaObjectModule* GetObjectModule() const { return m_propertyModuleConfiguration->GetMetaObject(); }

	// The session module — where SetSessionParameters lives. Null-safe by the same
	// rule as the configuration module: a configuration that declares no session
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
					child->GetIcon(),
					child);
			}
			return true;
		}
		return false;
	}

	bool FillRoleList(ibPropertyList* prop)     { return FillListByClsid(prop, g_metaRoleCLSID); }
	bool FillLanguageList(ibPropertyList* prop) { return FillListByClsid(prop, g_metaLanguageCLSID); }

	ibHomePageDescription m_homePage;

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyModuleConfiguration = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("ConfigurationModule"), _("Configuration module"));

	// THE SESSION MODULE — a second module on the root, and the only place a session
	// parameter may be written. It carries one procedure, SetSessionParameters, run
	// once per session before anything reads data: the values it sets are what row
	// access is filtered by, so they have to exist before the first query and stay
	// unchanged after it.
	//
	// Separate from the configuration module rather than another handler inside it,
	// because the two run at different moments and for different audiences. The
	// configuration module speaks to an interactive client — BeforeStart can refuse
	// a login, OnStart opens the desktop — and never runs for a background job. This
	// one runs for EVERY session, job included, and runs earlier.
	// A MANAGER module, not a plain one — and that is not a style choice. A plain
	// ibValueMetaObjectModule never registers itself with the module storage, so it
	// is never compiled into a session and its procedure can never be called: the
	// module would exist in the tree, open in the editor, and quietly do nothing.
	// A manager module registers (AddCommonModule) and is compiled once with the
	// session's modules, which is exactly what a scheduled job's handler relies on.
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyModuleSession = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("SessionModule"), _("Session module"));

	ibPropertyCategory* m_propertyPresetValues = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefRole = ibPropertyObject::CreateProperty<ibPropertyList>(m_propertyPresetValues, wxT("DefaultRole"), _("Default role"), _("Default configuration role"), &ibValueMetaObjectConfiguration::FillRoleList);
	ibPropertyList* m_propertyDefLanguage = ibPropertyObject::CreateProperty<ibPropertyList>(m_propertyPresetValues, wxT("DefaultLanguage"), _("Default language"), _("Default configuration language"), &ibValueMetaObjectConfiguration::FillLanguageList);

	ibPropertyCategory* m_compatibilityCategory = ibPropertyObject::CreatePropertyCategory(wxT("Compatibility"), _("Compatibility"));
	ibPropertyEnum<ibValueEnumVersion>* m_propertyVersion = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumVersion>>(m_compatibilityCategory, wxT("Version"), _("Version"), version_oes_last);
	// CES is the default for new configurations. VES (Visual Basic-style
	// ES, a legacy business-scripting dialect) is kept available for legacy / migrated
	// configurations and acts as a "please migrate" signal in the
	// metadata UI.
	ibPropertyEnum<ibValueEnumSyntax>* m_propertySyntax = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSyntax>>(m_compatibilityCategory, wxT("Syntax"), _("Syntax"), syntax_ces);

#pragma region role 
	ibRole* m_roleAdministration = ibValueMetaObject::CreateRole(wxT("Administration"), _("Administration"));
	ibRole* m_roleDataAdministration = ibValueMetaObject::CreateRole(wxT("DataAdministration"), _("Data administration"));
	ibRole* m_roleUpdateDatabaseConfiguration = ibValueMetaObject::CreateRole(wxT("UpdateDatabaseConfiguration"), _("Update database configuration"));
	ibRole* m_roleActiveUsers = ibValueMetaObject::CreateRole(wxT("ActiveUsers"), _("Active users"));
	ibRole* m_roleExclusiveMode = ibValueMetaObject::CreateRole(wxT("ExclusiveMode"), _("Exclusive mode"));
	ibRole* m_roleModeAllFunction = ibValueMetaObject::CreateRole(wxT("ModeAllFunctions"), _("Mode \"All functions\""));
#pragma endregion
};

#endif 