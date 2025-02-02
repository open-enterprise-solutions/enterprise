#ifndef _BASE__FRAME__H__
#define _BASE__FRAME__H__

#include <wx/wx.h>

class CProcUnit;

#include "frontend/frontend.h"

#include "backend/compiler/value/value.h"
#include "backend/wrapper/propertyInfo.h"
#include "backend/wrapper/typeInfo.h"

#include "backend/backend_form.h"

#include "frontend/visualView/formdefs.h"
#include "frontend/visualView/controlCtor.h"
#include "frontend/visualView/visual.h"

class ISourceDataObject;
class BACKEND_API IRecordDataObject;
class BACKEND_API IListDataObject;

class FRONTEND_API CValueForm;

class FRONTEND_API CVisualEditorHost;
class FRONTEND_API CVisualHost;

class BACKEND_API IMetaObjectForm;

#include "backend/wrapper/actionInfo.h"
#include "backend/wrapper/moduleInfo.h"

#include "backend/fileSystem/fs.h"

class FRONTEND_API CVisualDocument;
class CSourceExplorer;

#include "frontend/visualView/special/enums/valueOrient.h"

class FRONTEND_API IControlFrame : public IBackendControlFrame {
public:

	virtual bool GetControlValue(CValue& pvarControlVal) const {
		return false;
	}

	virtual Guid GetControlGuid() const {
		return Guid::newGuid();
	};

	//get visual document
	virtual CVisualDocument* GetVisualDocument() const {
		return nullptr;
	};

	virtual CValueForm* GetOwnerForm() const {
		return nullptr;
	};

	virtual bool HasQuickChoice() const = 0;
	virtual void ChoiceProcessing(CValue& vSelected) = 0;
};

class FRONTEND_API IValueFrame : public CValue,
	public IPropertyObject, public IControlFrame, public IActionSource {
	wxDECLARE_ABSTRACT_CLASS(IValueFrame);
protected:

	enum {
		eProperty,
		eControl,
		eEvent,
		eSizerItem,
	};

	//object of methods 
	CMethodHelper* m_methodHelper;

private:

	IValueFrame* DoFindControlByID(const form_identifier_t& id, IValueFrame* control);
	IValueFrame* DoFindControlByName(const wxString& controlName, IValueFrame* control);
	void DoGenerateNewID(form_identifier_t& id, IValueFrame* top);

public:

	OptionList* GetOrient(PropertyEnumOption<CValueEnumOrient>* property) {
		OptionList* optList = new OptionList();
		optList->AddOption(_("vertical"), wxVERTICAL);
		optList->AddOption(_("horizontal"), wxHORIZONTAL);
		return optList;
	}

public:

	IValueFrame();
	virtual ~IValueFrame();

	//system override 
	virtual wxString GetClassName() const final;
	virtual wxString GetObjectTypeName() const final;

	// Gets the parent object
	template <typename retType = IValueFrame >
	inline retType* GetParent() const {
		return wxDynamicCast(m_parent, retType);
	}

	/**
	* Obtiene un hijo del objeto.
	*/
	IValueFrame* IValueFrame::GetChild(unsigned int idx) const {
		return wxDynamicCast(IPropertyObject::GetChild(idx), IValueFrame);
	}

	IValueFrame* IValueFrame::GetChild(unsigned int idx, const wxString& type) const {
		return wxDynamicCast(IPropertyObject::GetChild(idx, type), IValueFrame);
	}

	IValueFrame* FindNearAncestor(const wxString& type) const {
		return wxDynamicCast(IPropertyObject::FindNearAncestor(type), IValueFrame);
	}

	IValueFrame* FindNearAncestorByBaseClass(const wxString& type) const {
		return wxDynamicCast(IPropertyObject::FindNearAncestorByBaseClass(type), IValueFrame);
	}

	/**
	* Support generate id
	*/
	virtual form_identifier_t GenerateNewID();

	/**
	* Support get/set object id
	*/
	virtual bool SetControlID(const form_identifier_t& id) {
		if (id > 0) {
			IValueFrame* foundedControl =
				FindControlByID(id);
			wxASSERT(foundedControl == nullptr);
			if (foundedControl == nullptr) {
				m_controlId = id;
				return true;
			}
		}
		else {
			m_controlId = id;
			return true;
		}
		return false;
	}

	virtual form_identifier_t GetControlID() const {
		return m_controlId;
	}

	virtual void SetControlName(const wxString& controlName) {};
	virtual wxString GetControlName() const = 0;

	void ResetGuid() {
		wxASSERT(m_controlGuid.isValid()); m_controlGuid.reset();
	}

	void GenerateGuid() {
		wxASSERT(!m_controlGuid.isValid()); m_controlGuid = wxNewUniqueGuid;
	}

	virtual Guid GetControlGuid() const {
		return m_controlGuid;
	}

	/**
	* Find by control id
	*/
	virtual IValueFrame* FindControlByName(const wxString& controlName);
	virtual IValueFrame* FindControlByID(const form_identifier_t& id);

	/**
	* Support form
	*/
	virtual CValueForm* GetOwnerForm() const = 0;
	virtual void SetOwnerForm(CValueForm* ownerForm) {};

	/**
	* Support default menu
	*/
	virtual void PrepareDefaultMenu(wxMenu* m_menu) {}
	virtual void ExecuteMenu(IVisualHost* visualHost, int id) {}

	/**
	* Get wxObject from visual view (if exist)
	*/
	wxObject* GetWxObject() const;

	/**
	Sets whether the object is expanded in the object tree or not.
	*/
	void SetExpanded(bool expanded) {
		m_expanded = expanded;
	}

	/**
	Gets whether the object is expanded in the object tree or not.
	*/
	bool GetExpanded() const {
		return m_expanded;
	}

	//get metaData
	virtual IMetaData* GetMetaData() const = 0;

	/**
	* Can delete object
	*/
	virtual bool CanDeleteControl() const = 0;

	// filter data 
	virtual bool FilterSource(const CSourceExplorer& src, const meta_identifier_t& id);

public:

	/**
	* Create an instance of the wxObject and return a pointer
	*/
	virtual wxObject* Create(wxWindow* wndParent, IVisualHost* visualHost) {
		return new wxNoObject;
	}

	/**
	* Allows components to do something after they have been created.
	* For example, Abstract components like NotebookPage and SizerItem can
	* add the actual widget to the Notebook or sizer.
	*
	* @param wxobject The object which was just created.
	* @param wxparent The wxWidgets parent - the wxObject that the created object was added to.
	*/
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool firstСreated) {};

	/**
	* Allows components to respond when selected in object tree.
	* For example, when a wxNotebook's page is selected, it can switch to that page
	*/
	virtual void OnSelected(wxObject* wxobject) {};

	/**
	* Allows components to do something after they have been updated.
	*/
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) {};

	/**
	* Allows components to do something after they have been updated.
	* For example, Abstract components like NotebookPage and SizerItem can
	* add the actual widget to the Notebook or sizer.
	*
	* @param wxobject The object which was just updated.
	* @param wxparent The wxWidgets parent - the wxObject that the updated object was added to.
	*/
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost) {};

	/**
	 * Cleanup (do the reverse of Create)
	 */
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) {};

public:

	// call current event
	template <typename ...Types>
	bool CallAsEvent(const Event* event, Types&... args) {
		if (event == nullptr)
			return false;
		const wxString& eventValue = event->GetValue();
		CProcUnit* formProcUnit = GetFormProcUnit();
		if (formProcUnit != nullptr && !eventValue.IsEmpty()) {
			CValue eventCancel = false;
			try {
				formProcUnit->CallAsProc(
					event->GetValue(), //event name
					args...,
					eventCancel
				);
			}
			catch (...) {
				return false;
			}
			return eventCancel.GetBoolean();
		}
		return false;
	}

	//call current form
	template <typename ...Types>
	bool CallAsEvent(const wxString& functionName, Types&... args) {
		CProcUnit* formProcUnit = GetFormProcUnit();
		if (formProcUnit != nullptr && !functionName.IsEmpty()) {
			try {
				formProcUnit->CallAsProc(
					functionName,
					args...
				);
			}
			catch (...) {
				return false;
			}
			return true;
		}
		return false;
	}

public:

	//get visual doc
	virtual CVisualDocument* GetVisualDocument() const;

	virtual bool HasQuickChoice() const;
	virtual void ChoiceProcessing(CValue& vSelected) {}

	IVisualEditorNotebook* FindVisualEditor() const;

public:

	//support actionData 
	virtual actionData_t GetActions(const form_identifier_t& formType) override { return actionData_t(); }
	virtual void ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm) override {}

	class CValueEventContainer : public CValue {
		CMethodHelper* m_methodHelper;
		IValueFrame* m_controlEvent;
	public:

		CValueEventContainer();
		CValueEventContainer(IValueFrame* ownerEvent);
		virtual ~CValueEventContainer();

		virtual CMethodHelper* GetPMethods() const {  //получить ссылку на класс помощник разбора имен атрибутов и методов
			//PrepareNames(); 
			return m_methodHelper;
		}
		virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
		virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

		virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);
		virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal); //значение атрибута

		virtual bool SetAt(const CValue& varKeyValue, const CValue& varValue);
		virtual bool GetAt(const CValue& varKeyValue, CValue& pvarValue);

		//Расширенные методы:
		bool Property(const CValue& varKeyValue, CValue& cValueFound);
		unsigned int Count() const { return m_controlEvent->GetEventCount(); }

		//Работа с итераторами:
		virtual bool HasIterator() const { return true; }
		virtual CValue GetIteratorEmpty();
		virtual CValue GetIteratorAt(unsigned int idx);
		virtual unsigned int GetIteratorCount() const { return Count(); }

	private:
		wxDECLARE_DYNAMIC_CLASS(CValueEventContainer);
	};

	virtual bool GetControlValue(CValue& pvarControlVal) const {
		return false;
	}

	/**
	* Property events
	*/
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue);

	virtual bool OnEventChanging(Event* event, const wxString& newValue);
	virtual void OnEventChanged(Event* event, const wxVariant& oldValue, const wxVariant& newValue);

public:

	/**
	* Get type form
	*/
	virtual form_identifier_t GetTypeForm() const = 0;

	//runtime 
	virtual CProcUnit* GetFormProcUnit() const = 0;

	//methods 
	virtual CMethodHelper* GetPMethods() const {  //получить ссылку на класс помощник разбора имен атрибутов и методов
		//PrepareNames(); 
		return m_methodHelper;
	}
	virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов

	//attributes 
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);

	//check is empty
	virtual inline bool IsEmpty() const {
		return false;
	}

	virtual bool Init() final override;
	virtual bool Init(CValue** paParams, const long lSizeArray) final override;

public:

	//load & save object in metaObject 
	bool LoadControl(const IMetaObjectForm* metaForm, CMemoryReader& dataReader);
	bool SaveControl(const IMetaObjectForm* metaForm, CMemoryWriter& dataWritter = CMemoryWriter());

protected:

	virtual void OnChoiceProcessing(CValue& vSelected) {}

	//load & save event in control 
	virtual bool LoadEvent(CMemoryReader& reader);
	virtual bool SaveEvent(CMemoryWriter& writer = CMemoryWriter());

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader) { return true; }
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter()) { return true; }

protected:

	CValueEventContainer* m_valEventContainer;

	form_identifier_t	 m_controlId;
	Guid				 m_controlGuid;

	bool m_expanded = true; // is expanded in the object tree, allows for saving to file
};

#endif // !_BASE_H_
