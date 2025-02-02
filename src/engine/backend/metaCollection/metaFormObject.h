#ifndef _METAFORMOBJECT_H__
#define _METAFORMOBJECT_H__

#include "metaModuleObject.h"
#include "backend/uniqueKey.h"

#define defaultFormType 100
#define formDefaultName _("form")

class ISourceDataObject;

class BACKEND_API IMetaObjectForm : public CMetaObjectModule {
	wxDECLARE_ABSTRACT_CLASS(IMetaObjectForm);	
private:

	enum
	{
		ID_METATREE_OPEN_FORM = 19000,
	};

public:

	IMetaObjectForm(const wxString& strName = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	bool LoadFormData(IBackendValueForm* value);
	bool SaveFormData(IBackendValueForm *value);

	IBackendValueForm* GenerateForm(IBackendControlFrame* ownerControl = nullptr,
		ISourceDataObject* ownerSrc = nullptr, const CUniqueKey& guidForm = wxNullGuid);
	IBackendValueForm* GenerateFormAndRun(IBackendControlFrame* ownerControl = nullptr,
		ISourceDataObject* ownerSrc = nullptr, const CUniqueKey& guidForm = wxNullGuid);

	/**
	* Get type form
	*/
	virtual form_identifier_t GetTypeForm() const = 0;

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

protected:

	bool m_firstInitialized;
	wxMemoryBuffer m_formData;
};

class BACKEND_API CMetaObjectForm : public IMetaObjectForm {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectForm);
private:

	friend class CVisualEditor;

	friend class IMetaObjectRecordData;
	friend class IListDataObject;
	friend class IRecordDataObject;

protected:

	OptionList* GetFormType(PropertyOption*);

	PropertyCategory* m_categoryForm = IPropertyObject::CreatePropertyCategory("FormType");
	Property* m_properyFormType = IPropertyObject::CreateProperty(m_categoryForm, "formType", &CMetaObjectForm::GetFormType, wxNOT_FOUND);

public:

	CMetaObjectForm(const wxString& strName = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual void OnPropertySelected(Property* property);
	virtual void OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue);

	//events:
	virtual bool OnCreateMetaObject(IMetaData* metaData);
	virtual bool OnLoadMetaObject(IMetaData* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	/**
	* Get type form
	*/
	virtual form_identifier_t GetTypeForm() const {
		return m_properyFormType->GetValueAsInteger();
	}

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

class BACKEND_API CMetaObjectCommonForm : public IMetaObjectForm {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectCommonForm);
protected:
	Role* m_roleUse = IMetaObject::CreateRole({ "use", _("use") });
public:

	CMetaObjectCommonForm(const wxString& strName = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(IMetaData* metaData);

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	/**
	* Get type form
	*/
	virtual form_identifier_t GetTypeForm() const {
		return defaultFormType;
	}
};

#endif 