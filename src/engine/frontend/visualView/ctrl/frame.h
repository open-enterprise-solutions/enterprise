#ifndef _BASE__FRAME__H__
#define _BASE__FRAME__H__

#include <wx/wx.h>

#include "frontend/frontend.h"

#include "backend/compiler/value.h"
#include "backend/propertyManager/propertyManager.h"

#include "backend/backend_type.h"
#include "backend/backend_form.h"
#include "backend/backend_localization.h"

#include "frontend/visualView/formdefs.h"
#include "frontend/visualView/controlCtor.h"
#include "frontend/visualView/visualHost.h"

class BACKEND_API ibSourceExplorer;
class BACKEND_API ibProcUnit;

class BACKEND_API ibValueMetaObjectFormBase;

class BACKEND_API ibSourceDataObject;
class BACKEND_API ibValueListDataObject;
class BACKEND_API ibValueRecordDataObject;

class FRONTEND_API ibValueForm;
class FRONTEND_API ibVisualHostClient;

#include "backend/actionInfo.h"
#include "backend/moduleInfo.h"

#define wxDefaultStypeFGColour wxColour(0, 120, 215)
#define wxDefaultStypeBGColour wxColour(235, 235, 241)

#include "backend/fileSystem/fs.h"

class FRONTEND_API ibFormVisualDocument;

#include "frontend/visualView/controlEnum.h"

class FRONTEND_API ibControlFrame : public ibBackendControlFrame {
public:

	//get value control and guid 
	virtual bool GetControlValue(ibValue& pvarControlVal) const { return false; }
	virtual ibGuid GetControlGuid() const { return ibGuid::newGuid(); }

	//get owner form 
	virtual ibValueForm* GetOwnerForm() const { return nullptr; }

	//get ref class 
	virtual ibClassID GetClassType() const { return 0; }

	//get visual document
	virtual ibFormVisualDocument* GetVisualDocument() const { return nullptr; }

	virtual bool HasQuickChoice() const = 0;
	virtual void ChoiceProcessing(ibValue& vSelected) = 0;
};

class FRONTEND_API ibValueFrame : public ibValue,
	public ibPropertyObjectHelper<ibValueFrame>,
	public ibControlFrame,
	public ibActionDataObject {
	wxDECLARE_ABSTRACT_CLASS(ibValueFrame);
protected:

	enum {
		eProperty,
		eControl,
		eEvent,
		eSizerItem,
	};

private:

	ibValueFrame* DoFindControlByID(const ibFormID& id, ibValueFrame* control) const;
	ibValueFrame* DoFindControlByName(const wxString& controlName, ibValueFrame* control) const;
	void DoGenerateNewID(ibFormID& id, ibValueFrame* top) const;

public:

	void SetControlName(const wxString& controlName) { SetControlNameAsString(controlName); }
	wxString GetControlName() const {
		wxString result;
		GetControlNameAsString(result);
		return result;
	}

	ibValueFrame();
	virtual ~ibValueFrame();

	//system override 
	virtual wxString GetClassName() const final;
	virtual wxString GetObjectTypeName() const final;

	/**
	* Support generate id
	*/
	virtual ibFormID GenerateNewID();

	/**
	* Support get/set object id
	*/
	virtual bool SetControlID(const ibFormID& id) {
		if (id > 0) {
			ibValueFrame* foundedControl =
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

	virtual ibFormID GetControlID() const { return m_controlId; }

	/**
	* Support control name
	*/

	virtual bool GetControlNameAsString(wxString& result) const {
		result = GetObjectTypeName();
		return true;
	}

	virtual bool SetControlNameAsString(const wxString& result) const {
		return false;
	}

	// get control caption
	virtual wxString GetControlTitle() const {
		return wxGetTranslation(stringUtils::GenerateSynonym(GetClassName()));
	}

	virtual ibGuid GetControlGuid() const { return m_controlGuid; }

	/**
	* Find by control id
	*/
	virtual ibValueFrame* FindControlByName(const wxString& controlName) const;
	virtual ibValueFrame* FindControlByID(const ibFormID& id) const;

	/**
	* Support form
	*/
	virtual ibValueForm* GetOwnerForm() const = 0;
	virtual void SetOwnerForm(ibValueForm* ownerForm) {};

	/**
	* Support default menu
	*/
	virtual void PrepareDefaultMenu(wxMenu* menu) {}
	virtual void ExecuteMenu(ibVisualHost* visualHost, int id) {}

	/**
	* Get wxObject from visual view (if exist)
	*/
	wxObject* GetWxObject() const;

	/**
	Sets whether the object is expanded in the object tree or not.
	*/
	void SetExpanded(bool expanded) { m_expanded = expanded; }

	/**
	Gets whether the object is expanded in the object tree or not.
	*/
	bool GetExpanded() const { return m_expanded; }

	//get metaData
	virtual ibMetaData* GetMetaData() const = 0;

	/**
	* Can delete object
	*/
	virtual bool CanDeleteControl() const = 0;

public:

	// before/after run 
	virtual bool InitializeControl() { return true; }

public:

	/**
	* Create an instance of the wxObject and return a pointer
	*/
	virtual wxObject* Create(wxWindow* wndParent, ibVisualHost* visualHost) {
		return new ibNoObject;
	}

	/**
	* Allows components to do something after they have been created.
	* For example, Abstract components like NotebookPage and SizerItem can
	* add the actual widget to the Notebook or sizer.
	*
	* @param wxobject The object which was just created.
	* @param wxparent The wxWidgets parent - the wxObject that the created object was added to.
	*/
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) {};

	/**
	* Allows components to respond when selected in object tree.
	* For example, when a wxNotebook's page is selected, it can switch to that page
	*/
	virtual void OnSelected(wxObject* wxobject) {};

	/**
	* Allows components to do something after they have been updated.
	*/
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) {};

	/**
	* Allows components to do something after they have been updated.
	* For example, Abstract components like NotebookPage and SizerItem can
	* add the actual widget to the Notebook or sizer.
	*
	* @param wxobject The object which was just updated.
	* @param wxparent The wxWidgets parent - the wxObject that the updated object was added to.
	*/
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost) {};

	/**
	 * Cleanup (do the reverse of Create)
	 */
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) {};

public:

	// call current event
	template <typename ...Types>
	bool CallAsEvent(const ibEvent* event, Types&&... args) {
		if (event == nullptr)
			return false;
		const wxString& eventValue = event->GetValue();
		ibProcUnit* formProcUnit = GetFormProcUnit();
		if (formProcUnit != nullptr && !eventValue.IsEmpty()) {
			ibValue eventCancel = false;
			try {
				formProcUnit->CallAsProc(
					eventValue, //event name
					args...,
					eventCancel
				);
			}
			catch (...) {
				return false;
			}
			return eventCancel.GetBoolean();
		}

		return true;
	}

	//call current form
	template <typename ...Types>
	bool CallAsEvent(const wxString& functionName, Types&&... args) {
		ibProcUnit* formProcUnit = GetFormProcUnit();
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

		return true;
	}

public:

	virtual ibBackendValueForm* GetBackendForm() const;

	//get visual doc
	virtual ibFormVisualDocument* GetVisualDocument() const;

	virtual bool HasQuickChoice() const;
	virtual void ChoiceProcessing(ibValue& vSelected) {}

	ibFrontendVisualEditorNotebook* FindVisualEditor() const;

	//support printing 
	virtual wxPrintout* CreatePrintout() const { return nullptr; }

public:

	//support actionData 
	virtual ibActionCollection GetActionCollection(const ibFormID& formType) override { return ibActionCollection(); }
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm) override {}

	class ibValueEventContainer : public ibValue {
		wxDECLARE_DYNAMIC_CLASS(ibValueEventContainer);
	public:

		ibValueEventContainer();
		ibValueEventContainer(ibValueFrame* ownerEvent);
		virtual ~ibValueEventContainer();

		virtual ibValueMethodHelper* GetPMethods() const {  // get a reference to the class helper for parsing attribute and method names
			//PrepareNames(); 
			return m_methodHelper;
		}

		virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.
		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value

		virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue);
		virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

		//Расширенные методы:
		bool Property(const ibValue& varKeyValue, ibValue& cValueFound);
		unsigned int Count() const { return m_controlEvent->GetEventCount(); }

		//Работа с итераторами:
		virtual bool HasIterator() const { return true; }
		virtual ibValue GetIteratorEmpty();
		virtual ibValue GetIteratorAt(unsigned int idx);
		virtual unsigned int GetIteratorCount() const { return Count(); }

	private:
		ibValueFrame* m_controlEvent;
		ibValueMethodHelper* m_methodHelper;
	};

	virtual bool GetControlValue(ibValue& pvarControlVal) const { return false; }

	// memory reader form clpboard 
	static ibValueFrame* CreatePasteObject(const ibReaderMemory& reader,
		ibValueForm* dstForm, ibValueFrame* dstParent);

	/**
	* Property events
	*/
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

	virtual bool OnEventChanging(ibEvent* event, const wxString& newValue);
	virtual void OnEventChanged(ibEvent* event, const wxVariant& oldValue, const wxVariant& newValue);

	/**
	* Devuelve la posicion del hijo o GetChildCount() en caso de no encontrarlo
	*/
	bool ChangeChildPosition(ibValueFrame* obj, unsigned int pos);

	//copy & paste object 
	virtual bool CopyObject(ibWriterMemory& writer) const;
	virtual bool PasteObject(ibReaderMemory& reader);

public:

	/**
	* Get type form
	*/
	virtual ibFormID GetTypeForm() const = 0;

	//runtime 
	virtual ibProcUnit* GetFormProcUnit() const = 0;

	//counter
	virtual void ControlIncrRef() { ibValue::IncrRef(); }
	virtual void ControlDecrRef() { ibValue::DecrRef(); }

	//methods 
	virtual ibValueMethodHelper* GetPMethods() const {  // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}

	virtual void PrepareNames() const; // this method is automatically called to initialize attribute and method names.

	//attributes 
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	//check is empty
	virtual bool IsEmpty() const { return false; }

	virtual bool Init() final override;
	virtual bool Init(ibValue** paParams, const long lSizeArray) final override;

	//Get ref class 
	virtual ibClassID GetClassType() const {
		return ibValue::GetClassType();
	}

	virtual bool IsEditable() const;

public:

	static inline wxArrayString GetAllowedUserProperty() {

		wxArrayString arr;

		arr.Add(wxT("title"));
		arr.Add(wxT("minimum_size"));
		arr.Add(wxT("maximum_size"));
		arr.Add(wxT("font"));
		arr.Add(wxT("fg"));
		arr.Add(wxT("bg"));
		arr.Add(wxT("align"));
		arr.Add(wxT("stretch"));
		arr.Add(wxT("proportion"));
		arr.Add(wxT("orient"));
		arr.Add(wxT("tooltip"));
		arr.Add(wxT("visible"));

		return arr;
	}

	//load & save object in metaObject 
	bool LoadControl(const ibValueMetaObjectFormBase* metaForm, ibReaderMemory& dataReader);
	bool SaveControl(const ibValueMetaObjectFormBase* metaForm, ibWriterMemory dataWritter = ibWriterMemory(), bool copy_form = false);

protected:

	virtual void OnChangeChildPosition(ibValueFrame* obj, unsigned int pos) {}
	virtual void OnChoiceProcessing(ibValue& vSelected) {}

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader) { return true; }
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory()) { return true; }

protected:

	bool m_expanded = true; // is expanded in the object tree, allows for saving to file

	ibFormID	 m_controlId;
	ibGuid				 m_controlGuid;

	ibValuePtr<ibValueEventContainer> m_valEventContainer;

	//object of methods 
	ibValueMethodHelper* m_methodHelper;
};

#endif // !_BASE_H_
