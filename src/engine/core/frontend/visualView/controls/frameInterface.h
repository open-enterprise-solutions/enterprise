#ifndef _BASE_FRAME_H_
#define _BASE_FRAME_H_

#include <wx/wx.h>
#include <set>

class CProcUnit;

#include "core/compiler/value.h"
#include "core/common/propertyInfo.h"

#include "frontend/visualView/visualInterface.h"

class ISourceDataObject;
class IRecordDataObject;
class IListDataObject;

class CValueForm;

class CVisualEditorHost;
class CVisualHost;

class IMetaFormObject;

#include "core/common/actionInfo.h"
#include "core/common/moduleInfo.h"

#include "utils/fs/fs.h"

class CVisualDocument;
class CSourceExplorer;

#include "frontend/visualView/special/enums/valueOrient.h"

class IControlFrame {
public:

	virtual bool GetControlValue(CValue& pvarControlVal) const {
		return false;
	}

	virtual Guid GetControlGuid() const {
		return Guid::newGuid();
	};

	//get visual document
	virtual CVisualDocument* GetVisualDocument() const {
		return NULL;
	}

	virtual CValueForm* GetOwnerForm() const {
		return NULL; 
	}

	virtual bool HasQuickChoice() const = 0;
	virtual void ChoiceProcessing(CValue& vSelected) = 0;
};

class IValueFrame : public CValue,
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

	// Gets the parent object
	IValueFrame* GetParent() const {
		return wxDynamicCast(m_parent, IValueFrame);
	}

	/**
	* Obtiene un hijo del objeto.
	*/
	IValueFrame* GetChild(unsigned int idx) const;
	IValueFrame* GetChild(unsigned int idx, const wxString& type) const;

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
			wxASSERT(foundedControl == NULL);
			if (foundedControl == NULL) {
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

	CLASS_ID GetClsid() const {
		return m_controlClsid;
	}

	void SetClsid(const CLASS_ID& clsid) {
		m_controlClsid = clsid;
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

	//get metadata
	virtual IMetadata* GetMetaData() const = 0;

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
	bool CallEvent(const Event* event, Types&... args) {
		if (event == NULL)
			return false;
		const wxString& eventValue = event->GetValue();
		CProcUnit* formProcUnit = GetFormProcUnit();
		if (formProcUnit != NULL && !eventValue.IsEmpty()) {
			CValue eventCancel = false;
			try {
				formProcUnit->CallFunction(
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
	void CallFunction(const wxString& functionName, Types&... args) {
		CProcUnit* formProcUnit = GetFormProcUnit();
		if (formProcUnit != NULL && !functionName.IsEmpty()) {
			try {
				formProcUnit->CallFunction(functionName, args...);
			}
			catch (...) {
			}
		}
	}

public:

	//get visual doc
	virtual CVisualDocument* GetVisualDocument() const;

	virtual bool HasQuickChoice() const;
	virtual void ChoiceProcessing(CValue& vSelected) {}

	class CVisualEditorNotebook* FindVisualEditor() const; 

public:

	//support actions 
	virtual actionData_t GetActions(const form_identifier_t& formType) { return actionData_t(); }
	virtual void ExecuteAction(const action_identifier_t& lNumAction, CValueForm* srcForm) {}

	class CValueEventContainer : public CValue {
		CMethodHelper* m_methodHelper;
		IValueFrame* m_controlEvent;
	public:

		CValueEventContainer();
		CValueEventContainer(IValueFrame* ownerEvent);
		virtual ~CValueEventContainer();

		virtual CMethodHelper* GetPMethods() const { PrepareNames(); return m_methodHelper; } //получить ссылку на класс помощник разбора имен атрибутов и методов
		virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
		virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

		virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);
		virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal); //значение атрибута

		virtual bool SetAt(const CValue& varKeyValue, const CValue& varValue);
		virtual bool GetAt(const CValue& varKeyValue, CValue& pvarValue);

		virtual wxString GetTypeString() const { return wxT("events"); }
		virtual wxString GetString() const { return wxT("events"); }

		//Расширенные методы:
		bool Property(const CValue& varKeyValue, CValue& cValueFound);
		unsigned int Count() const { return m_controlEvent->GetEventCount(); }

		//Работа с итераторами:
		virtual bool HasIterator() const { return true; }
		virtual CValue GetItEmpty();
		virtual CValue GetItAt(unsigned int idx);
		virtual unsigned int GetItSize() const { return Count(); }

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
	virtual void OnPropertyChanged(Property* property);

	virtual bool OnEventChanging(Event* event, const wxString& newValue);

public:

	/**
	* Get type form
	*/
	virtual form_identifier_t GetTypeForm() const = 0;
	
	//runtime 
	virtual CProcUnit* GetFormProcUnit() const = 0;

	//methods 
	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов 
		PrepareNames();
		return m_methodHelper;
	}

	virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов

	//attributes 
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//check is empty
	virtual inline bool IsEmpty() const override {
		return false;
	}

public:

	//load & save object in metaObject 
	bool LoadControl(const IMetaFormObject* metaForm, CMemoryReader& dataReader);
	bool SaveControl(const IMetaFormObject* metaForm, CMemoryWriter& dataWritter = CMemoryWriter());

protected:

	virtual void OnChoiceProcessing(CValue& vSelected) {}

	//load & save event in control 
	virtual bool LoadEvent(CMemoryReader& reader);
	virtual bool SaveEvent(CMemoryWriter& writer = CMemoryWriter());

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader) { return true; }
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter()) { return true; }

protected:

	CLASS_ID m_controlClsid; //type object name
	form_identifier_t m_controlId;
	Guid m_controlGuid;

	bool m_expanded = true; // is expanded in the object tree, allows for saving to file
};

#endif // !_BASE_H_
