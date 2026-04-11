#ifndef __FORM_VALUE_H__
#define __FORM_VALUE_H__

#include "frontend/visualView/ctrl/control.h"

#define defaultFormId 1
#define thisForm wxT("ThisForm")

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class BACKEND_API ibValueType;
class BACKEND_API ibUniqueKey;

class FRONTEND_API ibFormVisualEditView;

class BACKEND_API ibValueMetaObjectFormBase;
class BACKEND_API ibValueMetaObjectGenericData;

//********************************************************************************************
//*                                 define commom clsid									     *
//********************************************************************************************

//COMMON FORM
const ibClassID g_controlFormCLSID = string_to_clsid("CT_FRME");

//********************************************************************************************
//*                                      Value Frame                                         *
//********************************************************************************************

class FRONTEND_API ibValueForm : 
	public ibBackendValueForm, public ibValueFrame, public ibModuleDataObject 
{
	wxDECLARE_DYNAMIC_CLASS(ibValueForm);

private:

	enum {
		eSystem = eSizerItem + 1,
		eProcUnit,
		eAttribute
	};

public:

	const ibUniqueKey& GetFormKey() const { return m_formKey; }
	bool CompareFormKey(const ibUniqueKey& formKey) const { return m_formKey == formKey; }

public:

	void SetCaption(const wxString& caption) { return m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	wxColour GetForegroundColour() const { return m_propertyFG->GetValueAsColour(); }
	wxColour GetBackgroundColour() const { return m_propertyBG->GetValueAsColour(); }

	bool IsFormEnabled() const { return m_propertyEnabled->GetValueAsBoolean(); }

	wxOrientation GetOrient() const { return m_propertyOrient->GetValueAsEnum(); }

	ibValueFrame* NewObject(const ibClassID& clsid, ibValueFrame* parentControl = nullptr, const ibValue& generateId = true);
	ibValueFrame* NewObject(const wxString& classControl, ibValueFrame* controlParent, const ibValue& generateId = true) {
		const ibClassID& clsid = ibValue::GetIDObjectFromString(classControl);
		if (clsid > 0) {
			return NewObject(
				ibValue::GetIDObjectFromString(classControl),
				controlParent,
				generateId
			);
		}
		return nullptr;
	}

	template <typename retType>
	inline retType* NewObject(const ibClassID& clsid, ibValueFrame* parentControl = nullptr, const ibValue& generateId = true) {
		return wxDynamicCast(
			NewObject(clsid, parentControl, generateId), retType);
	}

	template <typename retType>
	inline retType* NewObject(const wxString& className, ibValueFrame* parentControl = nullptr, const ibValue& generateId = true) {
		return wxDynamicCast(
			NewObject(className, parentControl, generateId), retType);
	}

	/**
	* Resuelve un posible conflicto de nombres.
	* @note el objeto a comprobar debe estar insertado en proyecto, por tanto
	*       no es válida para arboles "flotantes".
	*/
	void ResolveNameConflict(ibValueFrame* control);

	/**
	* Fabrica de objetos.
	* A partir del nombre de la clase se crea una nueva instancia de un objeto.
	*/
	ibValueFrame* CreateObject(const wxString& className, ibValueFrame* parentControl = nullptr);

	/**
	* Crea un objeto como copia de otro.
	*/
	static bool CopyObject(ibValueFrame* srcControl, bool copyOnPaste = true);
	static ibValueFrame* PasteObject(ibValueForm* dstForm, ibValueFrame* dstParent);

public:

	ibValueForm(const ibValueMetaObjectFormBase* creator = nullptr, ibControlFrame* ownerControl = nullptr,
		ibSourceDataObject* srcObject = nullptr, const ibUniqueKey& formGuid = wxNullUniqueKey);

	virtual ~ibValueForm();

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual void PrepareNames() const;

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//****************************************************************************
	//*                              Support form context                        *
	//****************************************************************************

	virtual void BuildForm(const ibFormID& formType);
	virtual void InitializeForm(const ibValueMetaObjectFormBase* creator, ibControlFrame* ownerControl,
		ibSourceDataObject* srcObject, const ibUniqueKey& formGuid);

	virtual bool InitializeFormModule();

	//get metaData
	virtual ibMetaData* GetMetaData() const;

	//runtime 
	virtual ibProcUnit* GetFormProcUnit() const { return m_procUnit; }
	virtual ibValueForm* GetImplValueRef() const override {
		return const_cast<ibValueForm*>(this);
	}

	virtual ibSourceDataObject* GetSourceObject() const { return m_sourceObject; }
	virtual const ibValueMetaObjectFormBase* GetFormMetaObject() const { return m_metaFormObject; }

	ibValueMetaObjectGenericData* GetMetaObject() const;

	// get control caption
	virtual wxString GetControlTitle() const;

	ibValue GetCreatedValue() const { return m_createdValue; }
	ibValue GetChangedValue() const { return m_changedValue; }

	virtual ibValueForm* GetOwnerForm() const {
		return const_cast<ibValueForm*>(this);
	}

	ibValueFrame* GetOwnerControl() const {
		return dynamic_cast<ibValueFrame*>(m_controlOwner);
	}

	/**
	* Get type form
	*/
	virtual ibFormID GetTypeForm() const;

	/**
	* Can delete object
	*/
	virtual bool CanDeleteControl() const { return false; }

	/**
	* Is editable object?
	*/
	virtual bool IsEditable() const;

public:

	class ibValueFormCollectionControl : public ibValue {
		wxDECLARE_DYNAMIC_CLASS(ibValueFormCollectionControl);
	public:
		ibValueFormCollectionControl();
		ibValueFormCollectionControl(ibValueForm* ownerFrame);
		virtual ~ibValueFormCollectionControl();

		virtual ibValueMethodHelper* GetPMethods() const {  // get a reference to the class helper for parsing attribute and method names
			//PrepareNames(); 
			return m_methodHelper;
		}

		virtual void PrepareNames() const;

		virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);
		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value
		virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

		//Расширенные методы:
		bool Property(const ibValue& varKeyValue, ibValue& cValueFound);
		unsigned int Count() const { return m_formOwner->m_listControl.size(); }

		//Работа с итераторами:
		virtual bool HasIterator() const { return true; }
		virtual ibValue GetIteratorEmpty();
		virtual ibValue GetIteratorAt(unsigned int idx);
		virtual unsigned int GetIteratorCount() const { return Count(); }
	private:
		ibValueForm* m_formOwner;
		ibValueMethodHelper* m_methodHelper;
	};

public:

	ibValueFrame* CreateControl(const wxString& classControl, ibValueFrame* control = nullptr);
	void RemoveControl(ibValueFrame* control);

public:

	virtual bool LoadForm(const wxMemoryBuffer& formData);
	bool LoadChildForm(ibReaderMemory& readerData, ibValueFrame* controlParent);
	virtual wxMemoryBuffer SaveForm();
	bool SaveChildForm(ibWriterMemory& writerData, ibValueFrame* controlParent);

	//notify
	virtual void NotifyCreate(const ibValue& vCreated);
	virtual void NotifyChange(const ibValue& vChanged);
	virtual void NotifyDelete(const ibValue& vChanged);

	virtual void NotifyChoice(ibValue& vSelected);

	ibValue CreateControl(const ibValueType* classControl, const ibValue& vControl);
	ibValue FindControl(const ibValue& vControl);
	void RemoveControl(const ibValue& vControl);

public:

	virtual void ActivateForm() { ActivateDocForm(); }
	virtual void RefreshForm() { RefreshDocForm(); }
	virtual void UpdateForm();
	virtual bool CloseForm(bool force = false);
	virtual void HelpForm();
	virtual void ChangeForm();

	virtual bool GenerateForm(ibValueRecordDataObjectRef* obj) const;
	virtual void ShowForm(ibBackendMetaDocument* docParent = nullptr, bool createContext = true) override;

	//set & get modify 
	virtual void Modify(bool modify = true);
	virtual bool IsModified() const { return m_formModified; }

	//shown form 
	virtual bool IsShown() const { return GetVisualDocument() != nullptr; }

	//support close form
	virtual void CloseOnChoice(bool close = true) { m_closeOnChoice = close; }
	virtual bool IsCloseOnChoice() const { return m_closeOnChoice; }

	virtual void CloseOnOwnerClose(bool close = true) { m_closeOnOwnerClose = close; }
	virtual bool IsCloseOnOwnerClose() const { return m_closeOnOwnerClose; }

	//timers 
	void AttachIdleHandler(const wxString& procedureName, int interval, bool single);
	void DetachIdleHandler(const wxString& procedureName);

	//get visual document
	virtual ibFormVisualDocument* GetVisualDocument() const;

	//special proc
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost);
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost);

	//actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

	virtual int GetComponentType() const { return COMPONENT_TYPE_FRAME; }
	
private:

	void ClearRecursive(ibValueFrame* control);

	//doc event
	bool CreateDocForm(ibMetaDocument* docParent, bool createContext = true);
	void ActivateDocForm();
	void ChoiceDocForm(ibValue& vSelected);
	void RefreshDocForm();
	bool CloseDocForm();

	void OnIdleHandler(wxTimerEvent& event) {

		if (m_procUnit != nullptr) {

			auto iterator = std::find_if(m_idleHandlerArray.begin(), m_idleHandlerArray.end(),
				[event](const auto pair) { return pair.second == event.GetEventObject(); }
			);

			if (iterator != m_idleHandlerArray.end())
				CallAsEvent(iterator->first);
		}

		event.Skip();
	}

	enum
	{
		eDataBlock = 0x3550,
		eChildBlock = 0x3570
	};

	ibValue					m_createdValue;
	ibValue					m_changedValue;

	ibFormID		m_formType;
	ibUniqueKey				m_formKey;

	bool					m_formModified;

	bool					m_closeOnChoice;
	bool					m_closeOnOwnerClose;

	const ibValueMetaObjectFormBase* m_metaFormObject; // ref to metaData

	ibControlFrame* m_controlOwner;
	ibSourceDataObject* m_sourceObject;

	std::set<ibValueControl*> m_listControl;
	std::map<wxString, wxTimer*> m_idleHandlerArray;

	ibValuePtr<ibValueFormCollectionControl> m_formCollectionControl;

	ibPropertyCategory* m_categoryFrame = ibPropertyObject::CreatePropertyCategory(wxT("Frame"), _("Frame"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryFrame, wxT("Title"), _("Title"), wxT(""));
	ibPropertyColour* m_propertyFG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryFrame, wxT("ForegroundColour"), _("Foreground"), _("Sets the foreground colour of the window."), wxDefaultStypeFGColour);
	ibPropertyColour* m_propertyBG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryFrame, wxT("BackgroundColour"), _("Background"), _("Sets the background colour of the window."), wxDefaultStypeBGColour);
	ibPropertyBoolean* m_propertyEnabled = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryFrame, wxT("Enabled"), _("Enabled"), _("Enable or disable the window for user input.Note that when a parent window is disabled, all of its children are disabled as well and they are reenabled again when the parent is."), true);
	ibPropertyCategory* m_categorySizer = ibPropertyObject::CreatePropertyCategory(wxT("Sizer"), _("Sizer"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySizer, wxT("Orient"), _("Orient"), wxVERTICAL);

	friend class ibValueControl;
	friend class ibValueFormCollectionControl;

	friend class ibFormVisualDocument;
	friend class ibFormVisualEditView;
};

#endif 
