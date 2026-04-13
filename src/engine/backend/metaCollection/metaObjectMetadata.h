#ifndef __METAOBJECT_METADATA_H__
#define __METAOBJECT_METADATA_H__

#include "metaObject.h"
#include "metaObjectMetadataEnum.h"

#include "metaModuleObject.h"

//*****************************************************************************************
//*                                  metaData object                                      *
//*****************************************************************************************

#define configurationDefaultName _("Configuration")

///////////////////////////////////////////////////////////////////////////

class BACKEND_API ibValueMetaObjectConfiguration : public ibValueMetaObject {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectConfiguration);

	enum
	{
		ID_METATREE_OPEN_INIT_MODULE = 19000,
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

	virtual bool FilterChild(const ibClassID& clsid) const {

		if (
			clsid == g_metaCommonModuleCLSID ||
			clsid == g_metaCommonFormCLSID ||
			clsid == g_metaCommonTemplateCLSID ||
			clsid == g_metaRoleCLSID ||
			clsid == g_metaInterfaceCLSID ||
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
			clsid == g_metaChartOfCharacteristicTypesCLSID ||
			clsid == g_metaChartOfAccountsCLSID ||
			clsid == g_metaAccountingRegisterCLSID
			)
			return true;

		return false;
	}

	ibProgramSyntax GetCompileSyntax() const { return m_propertySyntax->GetValueAsEnum(); }

	void SetVersion(const ibVersionID& version) { m_propertyVersion->SetValue(static_cast<ibProgramVersion>(version)); }
	ibVersionID GetVersion() const { return m_propertyVersion->GetValueAsInteger(); }

	void SetLanguage(const ibMetaID& id) { m_propertyDefLanguage->SetValue(id); }
	ibMetaID GetLanguage() const { return m_propertyDefLanguage->GetValueAsInteger(); }

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

	virtual ibValueMetaObjectModule* GetModuleObject() const { return m_propertyModuleConfiguration->GetMetaObject(); }

protected:

	//load & save metaData from DB 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

private:

	bool FillRoleList(ibPropertyList* prop) {
		std::vector<ibValueMetaObject*> array;
		if (FillArrayObjectByFilter(array, { g_metaRoleCLSID })) {
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

	bool FillLanguageList(ibPropertyList* prop) {
		std::vector<ibValueMetaObject*> array;
		if (FillArrayObjectByFilter(array, { g_metaLanguageCLSID })) {
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

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyModuleConfiguration = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("ConfigurationModule"), _("Configuration module"));

	ibPropertyCategory* m_propertyPresetValues = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefRole = ibPropertyObject::CreateProperty<ibPropertyList>(m_propertyPresetValues, wxT("DefaultRole"), _("Default role"), _("Default configuration role"), &ibValueMetaObjectConfiguration::FillRoleList);
	ibPropertyList* m_propertyDefLanguage = ibPropertyObject::CreateProperty<ibPropertyList>(m_propertyPresetValues, wxT("DefaultLanguage"), _("Default language"), _("Default configuration language"), &ibValueMetaObjectConfiguration::FillLanguageList);

	ibPropertyCategory* m_compatibilityCategory = ibPropertyObject::CreatePropertyCategory(wxT("Compatibility"), _("Compatibility"));
	ibPropertyEnum<ibValueEnumVersion>* m_propertyVersion = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumVersion>>(m_compatibilityCategory, wxT("Version"), _("Version"), version_oes_last);
	ibPropertyEnum<ibValueEnumSyntax>* m_propertySyntax = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumSyntax>>(m_compatibilityCategory, wxT("Syntax"), _("Syntax"), syntax_vbs);

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