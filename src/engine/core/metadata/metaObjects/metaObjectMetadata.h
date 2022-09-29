#ifndef _METAOBJECT_METADATA_H__
#define _METAOBJECT_METADATA_H__

#include "metaObject.h"

//*****************************************************************************************
//*                                  metadata object                                      *
//*****************************************************************************************

#define configurationDefaultName _("ñonfiguration")

class CMetaObject : public IMetaObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaObject);
private:
	Role* m_roleAdministration = IMetaObject::CreateRole({ "administration", _("administration") });
	Role* m_roleDataAdministration = IMetaObject::CreateRole({ "dataAdministration", _("data administration") });
	Role* m_roleUpdateDatabaseConfiguration = IMetaObject::CreateRole({ "updateDatabaseConfiguration", _("update database configuration") });
	Role* m_roleActiveUsers = IMetaObject::CreateRole({ "activeUsers", _("active users") });
	Role* m_roleExclusiveMode = IMetaObject::CreateRole({ "exclusiveMode", _("exclusive mode") });
	Role* m_roleModeAllFunction = IMetaObject::CreateRole({ "modeAllFunctions", _("mode \"All functions\"") });

protected:
	enum
	{
		ID_METATREE_OPEN_INIT_MODULE = 19000,
	};

	OptionList* GetVersion(PropertyOption* prop) {

		OptionList* opt = new OptionList;
		opt->AddOption(_("oes 1_0_0"), version_oes_1_0_0);
		opt->AddOption(_("oes last"), version_oes_last);
		return opt;
	}

	PropertyCategory* m_compatibilityCategory = IPropertyObject::CreatePropertyCategory({ "compatibility", _("compatibility") });
	Property* m_propertyVersion = IPropertyObject::CreateProperty(m_compatibilityCategory, { "version", _("version")}, &CMetaObject::GetVersion, version_oes_last);

public:

	virtual bool FilterChild(const CLASS_ID &clsid) const {

		if (
			clsid == g_metaCommonModuleCLSID ||
			clsid == g_metaCommonFormCLSID ||
			clsid == g_metaCommonTemplateCLSID ||
			clsid == g_metaRoleCLSID ||
			clsid == g_metaInterfaceCLSID ||
			clsid == g_metaConstantCLSID ||
			clsid == g_metaCatalogCLSID ||
			clsid == g_metaDocumentCLSID ||
			clsid == g_metaEnumerationCLSID ||
			clsid == g_metaDataProcessorCLSID ||
			clsid == g_metaReportCLSID ||
			clsid == g_metaInformationRegisterCLSID ||
			clsid == g_metaAccumulationRegisterCLSID
			)
			return true; 

		return false;
	}

	virtual void SetVersion(const version_identifier_t& version) {
		m_propertyVersion->SetValue(version);
	}

	version_identifier_t GetVersion() const {
		return m_propertyVersion->GetValueAsInteger();
	}

	CMetaObject();
	virtual ~CMetaObject();

	virtual wxString GetFullName() const {
		return configurationDefaultName;
	}

	virtual wxString GetModuleName() const {
		return configurationDefaultName;
	}

	//get object class 
	virtual wxString GetClassName() const override {
		return wxT("metadata");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//events
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnLoadMetaObject(IMetadata* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

public:

	virtual CMetaModuleObject* GetModuleObject() const {
		return m_commonModule;
	}

protected:

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

private:

	CMetaModuleObject* m_commonModule;
};

#endif 