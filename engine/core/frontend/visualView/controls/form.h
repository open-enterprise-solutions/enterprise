#ifndef _FORM_H_
#define _FORM_H_

#include "baseControl.h"

#define defaultFormId 1

#include "common/docInfo.h"
#include "common/moduleInfo.h"

#define thisForm wxT("thisForm")

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class CValueType;
class IMetaFormObject;

//********************************************************************************************
//*                          define commom clsid											 *
//********************************************************************************************

//COMMON FORM
const CLASS_ID g_controlFormCLSID = TEXT2CLSID("CT_FRME");

//********************************************************************************************
//*                                  Visual Document                                         *
//********************************************************************************************

#include "guid/guid.h"

class CVisualDocument : public CDocument
{
	CVisualView *m_visualHost;

public:

	//is demomode
	CVisualDocument() : CDocument(), m_guidForm() {
		m_visualHost = NULL;
	}

	//other cases 
	CVisualDocument(const Guid &guid) : CDocument(), m_guidForm(guid) {
		m_visualHost = NULL;
	}

	virtual ~CVisualDocument();

	virtual bool OnSaveModified() override;
	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool modify) override;
	virtual bool Save() override;
	virtual bool SaveAs() override;

	void SetVisualView(CVisualView *visualHost);

	CVisualView *GetVisualView() const {
		return m_visualHost;
	}

	Guid GetGuid() const { return m_guidForm; }

protected:

	Guid m_guidForm;
};

//********************************************************************************************
//*                                  Value Frame                                             *
//********************************************************************************************

class CValueForm : public IValueFrame,
	public IModuleInfo {
	wxDECLARE_DYNAMIC_CLASS(CValueForm);
protected:

	OptionList *GetOrient(Property *property)
	{
		OptionList *optList = new OptionList();
		optList->AddOption("Vertical", wxVERTICAL);
		optList->AddOption("Horizontal", wxHORIZONTAL);
		return optList;
	}

	bool m_formModified;

public:

	wxString m_caption = wxT("New frame");

	wxColour m_fg = RGB(0, 120, 215);
	wxColour m_bg = RGB(240, 240, 240);

	wxString m_tooltip;
	bool m_context_menu;
	wxString m_context_help;
	bool m_enabled = true;
	bool m_visible = true;

	long m_style = wxCAPTION;
	long m_extra_style = 0;
	long m_center = 0;

	wxOrientation m_orient;

private:

	IMetaFormObject *m_metaFormObject; // ref to metadata
	IDataObjectSource *m_sourceObject;

public:
	
	IValueControl *NewObject(const wxString &className, IValueFrame *parentControl = NULL, bool generateId = true);
	template <typename retType>
	inline retType *NewObject(const wxString &className, IValueFrame *parentControl = NULL, bool generateId = true) { return wxDynamicCast(
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
	IValueControl *CreateObject(const wxString &className, IValueFrame *parentControl = NULL);

	/**
	* Crea un objeto como copia de otro.
	*/
	IValueControl *CopyObject(IValueFrame *srcControl);

public:

	CValueForm();
	CValueForm(IValueFrame *ownerControl, IMetaFormObject *metaForm,
		IDataObjectSource *ownerSrc, const Guid &formGuid, bool readOnly = false);

	virtual ~CValueForm();

	virtual wxString GetClassName() const override { return wxT("form"); }
	virtual wxString GetObjectTypeName() const override { return wxT("form"); }

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************

	virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t &aParams);                   //значение атрибута
	virtual int FindAttribute(const wxString &sName) const;

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual void PrepareNames() const;                             // этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t &aParams);       // вызов метода

	//****************************************************************************
	//*                              Support form context                        *
	//****************************************************************************

	void BuildForm(form_identifier_t formType);

	void InitializeForm(IValueFrame *ownerControl, IMetaFormObject *metaForm,
		IDataObjectSource *ownerSrc, const Guid &formGuid, bool readOnly = false);

	bool InitializeFormModule();

	//get metadata
	virtual IMetadata *GetMetaData() const override;

	//get typelist 
	virtual OptionList *GetTypelist() const override;

	//runtime 
	virtual CProcUnit *GetFormProcUnit() const { return m_procUnit; }

	IDataObjectSource *GetSourceObject() const;
	IMetaFormObject *GetFormMetaObject() const;
	IMetaObjectValue *GetMetaObject() const;

	/**
	* Support form
	*/
	virtual CValueForm* GetOwnerForm() const {
		return const_cast<CValueForm *>(this);
	}

	virtual void SetOwnerForm(CValueForm *ownerForm) {
		wxASSERT(false);
	}

	IValueFrame *GetOwnerControl() const { return m_formOwner; }

	/**
	* Get type form
	*/
	virtual form_identifier_t GetTypeForm() const;

	/**
	* Can delete object
	*/
	virtual bool CanDeleteControl() const { return false; }

public:

	std::vector<IValueControl *> m_aControls;

	class CValueFormControl : public CValue {
		wxDECLARE_DYNAMIC_CLASS(CValueFormControl);
	public:
		CValueFormControl();
		CValueFormControl(CValueForm *ownerFrame);
		virtual ~CValueFormControl();

		virtual CMethods* GetPMethods() const { PrepareNames(); return m_methods; }; //получить ссылку на класс помощник разбора имен атрибутов и методов
		virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
		virtual CValue Method(methodArg_t &aParams);

		virtual CValue GetAttribute(attributeArg_t &aParams); //значение атрибута
		virtual CValue GetAt(const CValue &cKey);

		virtual wxString GetTypeString() const { return wxT("formControls"); }
		virtual wxString GetString() const { return wxT("formControls"); }

		//Расширенные методы:
		bool Property(const CValue &cKey, CValue &cValueFound);
		unsigned int Count() const { return m_formOwner->m_aControls.size(); }

		//Работа с итераторами:
		virtual bool HasIterator() const { return true; }
		virtual CValue GetItEmpty();
		virtual CValue GetItAt(unsigned int idx);
		virtual unsigned int GetItSize() const { return Count(); }
	private:
		CValueForm *m_formOwner;
		CMethods *m_methods;
	};

	class CValueFormData : public CValue {
		wxDECLARE_DYNAMIC_CLASS(CValueFormData);
	public:
		CValueFormData();
		CValueFormData(CValueForm *ownerFrame);
		virtual ~CValueFormData();

		virtual CMethods* GetPMethods() const { PrepareNames(); return m_methods; }; //получить ссылку на класс помощник разбора имен атрибутов и методов
		virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
		virtual CValue Method(methodArg_t &aParams);

		virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal);        //установка атрибута
		virtual CValue GetAttribute(attributeArg_t &aParams);                   //значение атрибута
		virtual void SetAt(const CValue &cKey, CValue &cVal);
		virtual CValue GetAt(const CValue &cKey);

		virtual wxString GetTypeString() const { return wxT("formData"); }
		virtual wxString GetString() const { return wxT("formData"); }

		//Расширенные методы:
		bool Property(const CValue &cKey, CValue &cValueFound);
		unsigned int Count() const;

		//Работа с итераторами:
		virtual bool HasIterator() const { return true; }
		virtual CValue GetItEmpty();
		virtual CValue GetItAt(unsigned int idx);
		virtual unsigned int GetItSize() const { return Count(); }
	private:
		CValueForm *m_formOwner;
		CMethods *m_methods;
	};

	CValueFormControl *m_formControls;
	CValueFormData *m_formData;

public:

	CVisualDocument *m_valueFormDocument;

protected:

	friend class CValueFormControl;

	friend class CVisualDocument;
	friend class CFormView;

	bool ShowDocumentForm(CDocument *docParent, bool demo = false);
	bool CloseFrame();

public:

	IValueControl *CreateControl(const wxString &classControl, IValueFrame *control = NULL);
	void RemoveControl(IValueControl *control);

private:

	void ClearRecursive(IValueFrame *control);

public:

	static CValueForm *FindFormByGuid(const Guid &guid);

	void NotifyChoice(CValue &vSelected);

	CValue CreateControl(const CValueType *classControl, const CValue &vControl);
	CValue FindControl(const CValue &vControl);
	void RemoveControl(const CValue &vControl);

public:

	void ShowForm(CDocument *docParent = NULL, bool demo = false);
	void ActivateForm();
	void UpdateForm();
	bool CloseForm();
	void HelpForm();

	//set&get modify 
	void Modify(bool modify) { m_formModified = modify; if (m_valueFormDocument) { m_valueFormDocument->Modify(m_formModified); } }
	bool IsModified() { return m_formModified; }
	//shown form 
	bool IsShown() { return m_valueFormDocument != NULL; }

	//timers 
	void AttachIdleHandler(const wxString &procedureName, int interval, bool single); 
	void DetachIdleHandler(const wxString &procedureName);

	//get visual document
	CVisualDocument *GetVisualDocument() const { return m_valueFormDocument; }

	//special proc
	virtual void Update(wxObject* wxobject, IVisualHost *visualHost);
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost);

	/**
	* Set property data
	*/
	virtual void SetPropertyData(Property *property, const CValue &srcValue);

	/**
	* Get property data
	*/
	virtual CValue GetPropertyData(Property *property);

	//actions
	virtual actionData_t GetActions(form_identifier_t formType);
	virtual void ExecuteAction(action_identifier_t action, CValueForm *srcForm);

	//load & save object in control 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

	virtual int GetComponentType() override { return COMPONENT_TYPE_FRAME; }
	virtual bool IsItem() override { return false; }

private:
	void OnIdleHandler(wxTimerEvent& event);
protected:

	Guid m_formGuid;
	form_identifier_t m_defaultFormType;
	IValueFrame *m_formOwner;

	std::map<wxString, wxTimer *> m_aIdleHandlers;
};

#endif 
