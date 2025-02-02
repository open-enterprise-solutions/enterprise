#ifndef _FORM_H_
#define _FORM_H_

#include "control.h"
#include "frontend/docView/docView.h"

#define defaultFormId 1
#define thisForm wxT("thisForm")

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class CValueType;
class CUniqueKey;

class CVisualView;

class IMetaObjectForm;
class IMetaObjectGenericData;

//********************************************************************************************
//*                                 define commom clsid									     *
//********************************************************************************************

//COMMON FORM
const class_identifier_t g_controlFormCLSID = string_to_clsid("CT_FRME");

//********************************************************************************************
//*                                  Visual Document & View                                  *
//********************************************************************************************

#include "backend/wrapper/valueInfo.h"

class CVisualDocument : public CMetaDocument {
	CVisualHost* m_visualHost;
public:

	CVisualView* GetFirstView() const;

	//is demomode
	CVisualDocument() : CMetaDocument(), m_guidForm(CUniqueKey()) {
		m_visualHost = nullptr;
	}

	//other cases 
	CVisualDocument(const CUniqueKey& guid) : CMetaDocument(), m_guidForm(guid) {
		m_visualHost = nullptr;
	}

	virtual ~CVisualDocument();

	virtual bool OnSaveModified() override;
	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool modify) override;
	virtual bool Save() override;
	virtual bool SaveAs() override;

	void SetVisualView(CVisualHost* visualHost);

	CVisualHost* GetVisualView() const {
		return m_visualHost;
	}

	CUniqueKey GetGuid() const {
		return m_guidForm;
	}

protected:

	CUniqueKey m_guidForm;
};

class CVisualView : public CMetaView {
	CValueForm* m_valueForm;
public:

	CVisualView(CValueForm* valueForm) : m_valueForm(valueForm) {}

	virtual wxPrintout* OnCreatePrintout() override;
	virtual void OnUpdate(wxView* sender, wxObject* hint = nullptr) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	CValueForm* GetValueForm() const {
		return m_valueForm;
	}
};

//********************************************************************************************
//*                                      Value Frame                                         *
//********************************************************************************************

class FRONTEND_API CValueForm : public IBackendValueForm, public IValueFrame, public IModuleInfo {
	wxDECLARE_DYNAMIC_CLASS(CValueForm);
private:
	enum {
		eSystem = eSizerItem + 1,
		eProcUnit
	};
protected:
	PropertyCategory* m_categoryFrame = IPropertyObject::CreatePropertyCategory({ "frame", _("frame") });
	Property* m_propertyCaption = IPropertyObject::CreateProperty(m_categoryFrame, "caption", PropertyType::PT_WXSTRING, _("New frame"));
	Property* m_propertyFG = IPropertyObject::CreateProperty(m_categoryFrame, "fg", PropertyType::PT_WXCOLOUR, wxColour(0, 120, 215));
	Property* m_propertyBG = IPropertyObject::CreateProperty(m_categoryFrame, "bg", PropertyType::PT_WXCOLOUR, wxColour(240, 240, 240));
	Property* m_propertyEnabled = IPropertyObject::CreateProperty(m_categoryFrame, "enabled", PropertyType::PT_BOOL, true);
	PropertyCategory* m_categorySizer = IPropertyObject::CreatePropertyCategory({ "sizer", _("sizer") });
	Property* m_propertyOrient = IPropertyObject::CreateProperty(m_categorySizer, "orient", &IValueFrame::GetOrient, wxVERTICAL);
private:

	bool m_formModified;

	IMetaObjectForm* m_metaFormObject; // ref to metaData
	ISourceDataObject* m_sourceObject;

public:

	void SetCaption(const wxString& caption) {
		return m_propertyCaption->SetValue(caption);
	}

	wxString GetCaption() const {
		return m_propertyCaption->GetValueAsString();
	}

	wxColour GetForegroundColour() const {
		return m_propertyFG->GetValueAsColour();
	}

	wxColour GetBackgroundColour() const {
		return m_propertyBG->GetValueAsColour();
	}

	bool IsFormEnabled() const {
		return m_propertyEnabled->GetValueAsBoolean();
	}

	wxOrientation GetOrient() const {
		return (wxOrientation)m_propertyOrient->GetValueAsInteger();
	}

	IValueFrame* NewObject(const class_identifier_t& clsid, IValueFrame* parentControl = nullptr, const CValue &generateId = true);
	IValueFrame* NewObject(const wxString& classControl, IValueFrame* controlParent, const CValue& generateId = true) {
		const class_identifier_t& clsid = CValue::GetIDObjectFromString(classControl);
		if (clsid > 0) {
			return NewObject(
				CValue::GetIDObjectFromString(classControl),
				controlParent,
				generateId
			);
		}
		return nullptr;
	}

	template <typename retType>
	inline retType* NewObject(const class_identifier_t& clsid, IValueFrame* parentControl = nullptr, const CValue& generateId = true) {
		return wxDynamicCast(
			NewObject(clsid, parentControl, generateId), retType);
	}

	template <typename retType>
	inline retType* NewObject(const wxString& className, IValueFrame* parentControl = nullptr, const CValue& generateId = true) {
		return wxDynamicCast(
			NewObject(className, parentControl, generateId), retType);
	}

	/**
	* Resuelve un posible conflicto de nombres.
	* @note el objeto a comprobar debe estar insertado en proyecto, por tanto
	*       no es válida para arboles "flotantes".
	*/
	void ResolveNameConflict(IValueFrame* control);

	/**
	* Fabrica de objetos.
	* A partir del nombre de la clase se crea una nueva instancia de un objeto.
	*/
	IValueFrame* CreateObject(const wxString& className, IValueFrame* parentControl = nullptr);

	/**
	* Crea un objeto como copia de otro.
	*/
	IValueFrame* CopyObject(IValueFrame* srcControl);

public:

	CValueForm(IControlFrame* ownerControl = nullptr, IMetaObjectForm* metaForm = nullptr,
		ISourceDataObject* ownerSrc = nullptr, const CUniqueKey& formGuid = wxNullUniqueKey, bool readOnly = false);
	
	virtual ~CValueForm();

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************

	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual void PrepareNames() const;

	virtual bool CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray);
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

	//****************************************************************************
	//*                              Support form context                        *
	//****************************************************************************

	virtual void BuildForm(const form_identifier_t& formType);
	virtual void InitializeForm(IControlFrame* ownerControl, IMetaObjectForm* metaForm,
		ISourceDataObject* ownerSrc, const CUniqueKey& formGuid, bool readOnly = false);
	virtual bool InitializeFormModule();

	//get metaData
	virtual IMetaData* GetMetaData() const;

	//runtime 
	virtual CProcUnit* GetFormProcUnit() const {
		return m_procUnit;
	}

	virtual CValueForm* GetImplValueRef() const override {
		return const_cast<CValueForm*>(this);
	}

	ISourceDataObject* GetSourceObject() const;
	IMetaObjectForm* GetFormMetaObject() const;
	IMetaObjectGenericData* GetMetaObject() const;

	/**
	* Support form
	*/
	virtual wxString GetControlName() const {
		return GetObjectTypeName();
	};

	virtual CValueForm* GetOwnerForm() const {
		return const_cast<CValueForm*>(this);
	}

	IValueFrame* GetOwnerControl() const {
		return (IValueFrame*)m_controlOwner;
	}

	/**
	* Get type form
	*/
	virtual form_identifier_t GetTypeForm() const;

	/**
	* Can delete object
	*/
	virtual bool CanDeleteControl() const {
		return false;
	}

public:

	std::vector<IValueControl*> m_aControls;

	class CValueFormControl : public CValue {
		wxDECLARE_DYNAMIC_CLASS(CValueFormControl);
	public:
		CValueFormControl();
		CValueFormControl(CValueForm* ownerFrame);
		virtual ~CValueFormControl();

		virtual CMethodHelper* GetPMethods() const {  //получить ссылку на класс помощник разбора имен атрибутов и методов
			//PrepareNames(); 
			return m_methodHelper;
		}
		virtual void PrepareNames() const;

		virtual bool CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray);
		virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

		virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal); //значение атрибута
		virtual bool GetAt(const CValue& varKeyValue, CValue& pvarValue);

		//Расширенные методы:
		bool Property(const CValue& varKeyValue, CValue& cValueFound);
		unsigned int Count() const { return m_formOwner->m_aControls.size(); }

		//Работа с итераторами:
		virtual bool HasIterator() const { return true; }
		virtual CValue GetIteratorEmpty();
		virtual CValue GetIteratorAt(unsigned int idx);
		virtual unsigned int GetIteratorCount() const { return Count(); }
	private:
		CValueForm* m_formOwner;
		CMethodHelper* m_methodHelper;
	};

	class CValueFormData : public CValue {
		wxDECLARE_DYNAMIC_CLASS(CValueFormData);
	public:
		CValueFormData();
		CValueFormData(CValueForm* ownerFrame);
		virtual ~CValueFormData();

		virtual CMethodHelper* GetPMethods() const {  //получить ссылку на класс помощник разбора имен атрибутов и методов
			//PrepareNames(); 
			return m_methodHelper;
		}
		virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
		virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

		virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
		virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута
		virtual bool SetAt(const CValue& varKeyValue, const CValue& varValue);
		virtual bool GetAt(const CValue& varKeyValue, CValue& pvarValue);

		//Расширенные методы:
		bool Property(const CValue& varKeyValue, CValue& cValueFound);
		unsigned int Count() const;

		//Работа с итераторами:
		virtual bool HasIterator() const { return true; }
		virtual CValue GetIteratorEmpty();
		virtual CValue GetIteratorAt(unsigned int idx);
		virtual unsigned int GetIteratorCount() const { return Count(); }
	private:
		CValueForm* m_formOwner;
		CMethodHelper* m_methodHelper;
	};

	CValueFormControl* m_formControls;
	CValueFormData* m_formData;

public:

	CVisualDocument* m_valueFormDocument;

protected:

	friend class CVisualDocument;
	friend class CVisualView;

	friend class CValueFormControl;

	bool CreateDocForm(CMetaDocument* docParent, bool demo = false);
	bool CloseDocForm();

public:

	IValueFrame* CreateControl(const wxString& classControl, IValueFrame* control = nullptr);
	void RemoveControl(IValueFrame* control);

private:

	void ClearRecursive(IValueFrame* control);

public:

	virtual bool LoadForm(const wxMemoryBuffer& formData);
	bool LoadChildForm(CMemoryReader& readerData, IValueFrame* controlParent);
	virtual wxMemoryBuffer SaveForm();
	bool SaveChildForm(CMemoryWriter& writterData, IValueFrame* controlParent);

	static CValueForm* FindFormByUniqueKey(const CUniqueKey& guid);
	static bool UpdateFormUniqueKey(const CUniquePairKey& guid);

	void NotifyChoice(CValue& vSelected);
	CValue CreateControl(const CValueType* classControl, const CValue& vControl);
	CValue FindControl(const CValue& vControl);
	void RemoveControl(const CValue& vControl);

public:

	virtual void ShowForm(IBackendMetaDocument* docParent = nullptr, bool demo = false) override;

	virtual void ActivateForm();
	virtual void UpdateForm();
	virtual bool CloseForm();
	virtual void HelpForm();

	virtual bool GenerateForm(IRecordDataObjectRef* obj) const;

	//set & get modify 
	virtual void Modify(bool modify = true) {
		if (m_valueFormDocument != nullptr) {
			m_valueFormDocument->Modify(modify);
		}
		m_formModified = modify;
	}

	virtual bool IsModified() const {
		return m_formModified;
	}

	//shown form 
	virtual bool IsShown() const {
		return m_valueFormDocument != nullptr;
	}

	//timers 
	void AttachIdleHandler(const wxString& procedureName, int interval, bool single);
	void DetachIdleHandler(const wxString& procedureName);

	//get visual document
	virtual CVisualDocument* GetVisualDocument() const {
		return m_valueFormDocument;
	}

	//special proc
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost);
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost);

	//actionData
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	virtual int GetComponentType() const {
		return COMPONENT_TYPE_FRAME;
	}

private:

	void OnIdleHandler(wxTimerEvent& event);

protected:

	enum
	{
		eDataBlock = 0x3550,
		eChildBlock = 0x3570
	};

	CUniqueKey				m_formKey;
	form_identifier_t		m_defaultFormType;

	IControlFrame* m_controlOwner;

	std::map<wxString, wxTimer*> m_aIdleHandlers;
};

#endif 
