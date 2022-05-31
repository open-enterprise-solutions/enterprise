#ifndef _METAFORMOBJECT_H__
#define _METAFORMOBJECT_H__

#include "metaModuleObject.h"
#include "common/uniqueKey.h"

class CValueForm;

#define defaultFormType 100
#define formDefaultName _("form")

class ISourceDataObject;

class IMetaFormObject : public CMetaModuleObject {
	wxDECLARE_ABSTRACT_CLASS(IMetaFormObject);
private:

	enum
	{
		ID_METATREE_OPEN_FORM = 19000,
	};

	enum
	{
		eDataBlock = 0x3550,
		eChildBlock = 0x3570
	};

	//loader/saver/deleter:
	CValueForm* LoadControl(const wxString& formData);
	bool LoadChildControl(CValueForm* valueForm, CMemoryReader& readerData, IValueFrame* parentObj);
	wxString SaveControl(CValueForm* valueForm);
	bool SaveChildControl(CValueForm* valueForm, CMemoryWriter& writterData, IValueFrame* parentObj);

public:

	void SaveFormData(CValueForm* valueForm);

	IMetaFormObject(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	/**
	* Get type form
	*/
	form_identifier_t GetTypeForm() const { return m_typeFrom; }

	CValueForm* GenerateForm(IValueFrame* ownerControl = NULL,
		ISourceDataObject* ownerSrc = NULL, const CUniqueKey& guidForm = Guid());

	CValueForm* GenerateFormAndRun(IValueFrame* ownerControl = NULL,
		ISourceDataObject* ownerSrc = NULL, const CUniqueKey& guidForm = Guid());

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defultMenu);
	virtual void ProcessCommand(unsigned int id);

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

protected:

	bool m_firstInitialized;
	form_identifier_t m_typeFrom;
	wxString m_formData;
};

class CMetaFormObject : public IMetaFormObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaFormObject);
private:

	friend class CVisualEditorContextForm;

	friend class IMetaObjectRecordData;
	friend class IListDataObject;
	friend class IRecordDataObject;

private:

	OptionList* GetFormType(Property*);

public:

	CMetaFormObject(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	virtual wxString GetClassName() const override { return wxT("form"); }

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual void OnPropertySelected(Property* property);
	virtual void OnPropertyChanged(Property* property);

	//events:
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnLoadMetaObject(IMetadata* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

class CMetaCommonFormObject : public IMetaFormObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaCommonFormObject);
public:

	CMetaCommonFormObject(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);
	virtual wxString GetClassName() const override { return wxT("commonForm"); }

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(IMetadata* metaData);

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();
};

#endif 