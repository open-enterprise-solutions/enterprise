#ifndef _DOCUMENT_H__
#define _DOCUMENT_H__

#include "baseObject.h"

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class CObjectDocumentValue;

//********************************************************************************************
//*                                  Factory & metadata                                      *
//********************************************************************************************

class CMetaObjectDocumentValue : public IMetaObjectRefValue {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectDocumentValue);

	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

	CMetaModuleObject *m_moduleObject;
	CMetaCommonModuleObject *m_moduleManager;

	enum
	{
		eFormObject = 1,
		eFormList,
		eFormSelect
	};

	virtual OptionList *GetFormType() override
	{
		OptionList *optionlist = new OptionList;

		optionlist->AddOption("formObject", eFormObject);
		optionlist->AddOption("formList", eFormList);
		optionlist->AddOption("formSelect", eFormSelect);

		return optionlist;
	}

private:

	//default form 
	int m_defaultFormObject;
	int m_defaultFormList;
	int m_defaultFormSelect;

	//default attributes 
	CMetaDefaultAttributeObject *m_attributeNumber;
	CMetaDefaultAttributeObject *m_attributeDate;
	CMetaDefaultAttributeObject *m_attributePosted;

private:

	OptionList *GetFormObject(Property *);
	OptionList *GetFormList(Property *);
	OptionList *GetFormSelect(Property *);

public:

	CMetaDefaultAttributeObject *GetDocNumber() const { return m_attributeNumber; }
	CMetaDefaultAttributeObject *GetDocDate() const { return m_attributeDate; }
	CMetaDefaultAttributeObject *GetDocPosted() const { return m_attributePosted; }

	//default constructor 
	CMetaObjectDocumentValue();
	virtual ~CMetaObjectDocumentValue();

	virtual wxString GetClassName() const { return wxT("document"); }

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//events: 
	virtual bool OnCreateMetaObject(IMetadata *metaData);
	virtual bool OnLoadMetaObject(IMetadata *metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//for designer 
	virtual bool OnReloadMetaObject();

	//module manager is started or exit 
	virtual bool OnRunMetaObject(int flags);
	virtual bool OnCloseMetaObject();

	//form events 
	virtual void OnCreateMetaForm(IMetaFormObject *metaForm);
	virtual void OnRemoveMetaForm(IMetaFormObject *metaForm);

	//get attribute code 
	virtual IMetaAttributeObject *GetAttributeForCode() const {
		return m_attributeNumber;
	}

	//override base objects 
	virtual std::vector<IMetaAttributeObject *> GetObjectAttributes() const override;

	//searched attributes 
	virtual std::vector<IMetaAttributeObject *> GetSearchedAttributes() const override;

	//create associate value 
	virtual CMetaFormObject *GetDefaultFormByID(form_identifier_t id);

	//create object data with metaForm
	virtual IDataObjectSource *CreateObjectData(IMetaFormObject *metaObject);

	//create empty object
	virtual IDataObjectRefValue *CreateObjectRefValue();
	virtual IDataObjectRefValue *CreateObjectRefValue(const Guid &guid);

	//create form with data 
	virtual CValueForm *CreateObjectValue(IMetaFormObject *metaForm);

	//support form 
	virtual CValueForm *GetObjectForm(const wxString &formName = wxEmptyString, IValueFrame *ownerControl = NULL, const Guid &formGuid = Guid());
	virtual CValueForm *GetListForm(const wxString &formName = wxEmptyString, IValueFrame *ownerControl = NULL, const Guid &formGuid = Guid());
	virtual CValueForm *GetSelectForm(const wxString &formName = wxEmptyString, IValueFrame *ownerControl = NULL, const Guid &formGuid = Guid());

	//descriptions...
	wxString GetDescription(const IObjectValueInfo *objValue) const;

	//get module object in compose object 
	virtual CMetaModuleObject *GetModuleObject() { return m_moduleObject; }
	virtual CMetaCommonModuleObject *GetModuleManager() { return m_moduleManager; }

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu *defultMenu);
	virtual void ProcessCommand(unsigned int id);

	//read and write property 
	virtual void ReadProperty() override;
	virtual	void SaveProperty() override;

protected:

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());

	friend class CObjectDocumentValue;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

#define thisObject wxT("thisObject")

class CObjectDocumentValue : public IDataObjectRefValue
{
public:

	CObjectDocumentValue(const CObjectDocumentValue &source);
	CObjectDocumentValue(CMetaObjectDocumentValue *metaObject);
	CObjectDocumentValue(CMetaObjectDocumentValue *metaObject, const Guid &guid);

	//****************************************************************************
	//*                              Support id's                                *
	//****************************************************************************

	virtual IDataObjectValue *CopyObjectValue() {
		return new CObjectDocumentValue(*this); 
	}

	//save modify 
	virtual bool SaveModify() { return WriteObject(); }

	//default methods
	virtual void FillObject(CValue& vFillObject);
	virtual CValue CopyObject();
	virtual bool WriteObject();
	virtual bool DeleteObject();

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual CMethods* GetPMethods() const;
	virtual void PrepareNames() const;
	virtual CValue Method(methodArg_t &aParams);

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal);
	virtual CValue GetAttribute(attributeArg_t &aParams);

	//support show 
	virtual void ShowFormValue(const wxString &formName = wxEmptyString, IValueFrame *owner = NULL);
	virtual CValueForm *GetFormValue(const wxString &formName = wxEmptyString, IValueFrame *owner = NULL);

	//support actions
	virtual actionData_t GetActions(form_identifier_t formType);
	virtual void ExecuteAction(action_identifier_t action, CValueForm *srcForm);
};

#endif