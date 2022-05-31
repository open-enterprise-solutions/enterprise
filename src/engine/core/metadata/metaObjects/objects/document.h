#ifndef _DOCUMENT_H__
#define _DOCUMENT_H__

#include "baseObject.h"

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

enum eDocumentWriteMode {
	ePosting,
	eUndoPosting,
	eWrite
};

enum eDocumentPostingMode {
	eRealTime,
	eRegular
};

class CObjectDocument;

#include "compiler/enumObject.h"

class CValueEnumDocumentWriteMode : public IEnumeration<eDocumentWriteMode> {
	wxDECLARE_DYNAMIC_CLASS(CValueEnumDocumentWriteMode);
public:

	CValueEnumDocumentWriteMode() : IEnumeration() { 
		InitializeEnumeration(); 
	}

	CValueEnumDocumentWriteMode(eDocumentWriteMode writeMode) : IEnumeration(writeMode) {
		InitializeEnumeration(writeMode);
	}

	virtual wxString GetTypeString() const override {
		return wxT("documentWriteMode");
	}

protected:

	void CreateEnumeration()
	{
		AddEnumeration(ePosting, wxT("posting"));
		AddEnumeration(eUndoPosting, wxT("undoPosting"));
		AddEnumeration(eWrite, wxT("write"));
	}

	virtual void InitializeEnumeration() override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration();
	}

	virtual void InitializeEnumeration(eDocumentWriteMode value) override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration(value);
	}
};

class CValueEnumDocumentPostingMode : public IEnumeration<eDocumentPostingMode> {
	wxDECLARE_DYNAMIC_CLASS(CValueEnumDocumentPostingMode);
public:

	CValueEnumDocumentPostingMode() : IEnumeration() { 
		InitializeEnumeration(); 
	}

	CValueEnumDocumentPostingMode(eDocumentPostingMode postingMode) : IEnumeration(postingMode) {
		InitializeEnumeration(postingMode);
	}

	virtual wxString GetTypeString() const override {
		return wxT("documentPostingMode");
	}

protected:

	void CreateEnumeration()
	{
		AddEnumeration(eRealTime, wxT("realTime"));
		AddEnumeration(eRegular, wxT("regular"));
	}

	virtual void InitializeEnumeration() override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration();
	}

	virtual void InitializeEnumeration(eDocumentPostingMode value) override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration(value);
	}
};

//********************************************************************************************
//*                                  Factory & metadata                                      *
//********************************************************************************************

class wxVariantRecordData : public wxVariantData {

	wxString MakeString() const;

public:

	void AppendRecord(meta_identifier_t record_id) {
		m_recordData.insert(record_id);
	}

	bool Contains(meta_identifier_t record_id) const {
		auto itFounded = m_recordData.find(record_id);
		return itFounded != m_recordData.end();
	}

	meta_identifier_t GetById(unsigned int idx) const {
		if (m_recordData.size() == 0)
			return wxNOT_FOUND;
		auto itStart = m_recordData.begin();
		std::advance(itStart, idx);
		return *itStart;
	}

	unsigned int GetCount() const {
		return m_recordData.size();
	}

	bool Eq(wxVariantData& data) const {
		return true;
	}

	wxVariantRecordData(IMetadata* metaData) : wxVariantData(), m_metaData(metaData) {}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const {
		str << MakeString();
		return true;
	}
#endif
	virtual bool Write(wxString& str) const {
		str = MakeString();
		return true;
	}

	wxString GetType() const {
		return wxT("wxVariantRecordData");
	}

protected:
	IMetadata* m_metaData;
	std::set<meta_identifier_t> m_recordData;
};

//////////////////////////////////////////////////////////////////////////////////////////////

class CMetaObjectDocument : public IMetaObjectRecordDataMutableRef {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectDocument);

	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

	CMetaModuleObject* m_moduleObject;
	CMetaCommonModuleObject* m_moduleManager;

	enum
	{
		eFormObject = 1,
		eFormList,
		eFormSelect
	};

	virtual OptionList* GetFormType() override
	{
		OptionList* optionlist = new OptionList;

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
	CMetaDefaultAttributeObject* m_attributeNumber;
	CMetaDefaultAttributeObject* m_attributeDate;
	CMetaDefaultAttributeObject* m_attributePosted;

	//variant 
	class recordData_t {
		std::set<meta_identifier_t> m_recordData;
	public:
		
		bool LoadData(CMemoryReader& dataReader);
		
		bool LoadFromVariant(const wxVariant& variant);
		
		bool SaveData(CMemoryWriter& dataWritter);
		
		void SaveToVariant(wxVariant& variant, IMetadata* metaData) const;

		std::set<meta_identifier_t>& GetData() {
			return m_recordData;
		}
	};

	recordData_t m_recordData;

private:

	recordData_t& GetRecordData() {
		return m_recordData;
	}

	OptionList* GetFormObject(Property*);
	OptionList* GetFormList(Property*);
	OptionList* GetFormSelect(Property*);

public:

	CMetaDefaultAttributeObject* GetDocumentNumber() const {
		return m_attributeNumber;
	}

	CMetaDefaultAttributeObject* GetDocumentDate() const {
		return m_attributeDate;
	}

	CMetaDefaultAttributeObject* GetDocumentPosted() const {
		return m_attributePosted;
	}

	//default constructor 
	CMetaObjectDocument();
	virtual ~CMetaObjectDocument();

	virtual wxString GetClassName() const {
		return wxT("document"); 
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyChanged(Property* property);

	//events: 
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnLoadMetaObject(IMetadata* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//for designer 
	virtual bool OnReloadMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//form events 
	virtual void OnCreateMetaForm(IMetaFormObject* metaForm);
	virtual void OnRemoveMetaForm(IMetaFormObject* metaForm);

	//get attribute code 
	virtual IMetaAttributeObject* GetAttributeForCode() const {
		return m_attributeNumber;
	}

	//override base objects 
	virtual std::vector<IMetaAttributeObject*> GetDefaultAttributes() const override;

	//searched attributes 
	virtual std::vector<IMetaAttributeObject*> GetSearchedAttributes() const override;

	//create associate value 
	virtual CMetaFormObject* GetDefaultFormByID(const form_identifier_t &id);

	//create object data with metaForm
	virtual ISourceDataObject* CreateObjectData(IMetaFormObject* metaObject);

	//create empty object
	virtual IRecordDataObjectRef* CreateObjectRefValue();
	virtual IRecordDataObjectRef* CreateObjectRefValue(const Guid& guid);

	//create form with data 
	virtual CValueForm* CreateObjectValue(IMetaFormObject* metaForm) override {
		return metaForm->GenerateFormAndRun(
			NULL, CreateObjectData(metaForm)
		);
	}

	//support form 
	virtual CValueForm* GetObjectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = Guid());
	virtual CValueForm* GetListForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = Guid());
	virtual CValueForm* GetSelectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = Guid());

	//descriptions...
	wxString GetDescription(const IObjectValueInfo* objValue) const;

	//get module object in compose object 
	virtual CMetaModuleObject* GetModuleObject() const { return m_moduleObject; }
	virtual CMetaCommonModuleObject* GetModuleManager() const { return m_moduleManager; }

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defultMenu);
	virtual void ProcessCommand(unsigned int id);

	//read and write property 
	virtual void ReadProperty() override;
	virtual	void SaveProperty() override;

protected:

	//load & save from variant
	bool LoadFromVariant(const wxVariant& variant);
	void SaveToVariant(wxVariant& variant, IMetadata* metaData) const;

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	friend class CObjectDocument;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

#define thisObject wxT("thisObject")

#define registerRecords wxT("registerRecords")

class CObjectDocument : public IRecordDataObjectRef
{
	class CRecordRegister : public CValue {
		CObjectDocument* m_document;
		std::map<meta_identifier_t, IRecordSetObject*> m_records;
	public:

		void CreateRecordSet();
		bool WriteRecordSet(); 
		bool DeleteRecordSet();
		void ClearRecordSet();

		CRecordRegister(CObjectDocument* currentDoc);
		virtual ~CRecordRegister();
		//standart override 
		virtual CMethods* GetPMethods() const;

		virtual void PrepareNames() const;
		virtual CValue Method(methodArg_t& aParams);

		virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);
		virtual CValue GetAttribute(attributeArg_t& aParams);

		//check is empty
		virtual inline bool IsEmpty() const override {
			return false;
		}
	protected:
		CMethods* m_methods;
	};
	CRecordRegister* m_registerRecords;
public:

	void UpdateRecordSet() {
		wxASSERT(m_registerRecords);
		m_registerRecords->ClearRecordSet();
		m_registerRecords->CreateRecordSet();
	}

	CObjectDocument(const CObjectDocument& source);
	CObjectDocument(CMetaObjectDocument* metaObject);
	CObjectDocument(CMetaObjectDocument* metaObject, const Guid& guid);
	virtual ~CObjectDocument();

	//****************************************************************************
	//*                              Support id's                                *
	//****************************************************************************

	virtual IRecordDataObject* CopyObjectValue() {
		return new CObjectDocument(*this);
	}

	//save modify 
	virtual bool SaveModify() {
		return WriteObject(eDocumentWriteMode::ePosting, eDocumentPostingMode::eRegular);
	}

	//default methods
	virtual void FillObject(CValue& vFillObject);
	virtual CValue CopyObject();
	virtual bool WriteObject(eDocumentWriteMode writeMode, eDocumentPostingMode postingMode);
	virtual bool DeleteObject();

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual CMethods* GetPMethods() const;
	virtual void PrepareNames() const;
	virtual CValue Method(methodArg_t& aParams);

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************

	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);
	virtual CValue GetAttribute(attributeArg_t& aParams);

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;

	//support show 
	virtual void ShowFormValue(const wxString& formName = wxEmptyString, IValueFrame* owner = NULL);
	virtual CValueForm* GetFormValue(const wxString& formName = wxEmptyString, IValueFrame* owner = NULL);

	//support actions
	virtual actionData_t GetActions(const form_identifier_t &formType);
	virtual void ExecuteAction(const action_identifier_t &action, CValueForm* srcForm);
};

#endif