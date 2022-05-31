#ifndef _BASE_FRAME_H_
#define _BASE_FRAME_H_

#include <wx/wx.h>
#include <set>

class CProcUnit;

#include "compiler/value.h"
#include "frontend/visualView/visualEditorBase.h"
#include "common/objectbase.h"

class ISourceDataObject;
class IRecordDataObject;
class IListDataObject;

class CValueForm;

class CVisualEditorHost;
class CVisualHost;

class IMetaFormObject;

#include "common/actionInfo.h"
#include "guid/guid.h"
#include "utils/fs/fs.h"

class CSourceExplorer;

class IValueFrame : public CValue,
	public IObjectBase, public IActionSource {
	wxDECLARE_ABSTRACT_CLASS(IValueFrame);
protected:
	//object of methods 
	CMethods* m_methods;
private:
	IValueFrame* DoFindControlByID(const form_identifier_t &id, IValueFrame* control);
	IValueFrame* DoFindControlByName(const wxString& controlName, IValueFrame* control);
	void DoGenerateNewID(form_identifier_t& id, IValueFrame* top);
public:

	IValueFrame();
	virtual ~IValueFrame();

	// Gets the parent object
	IValueFrame* GetParent() const { return wxDynamicCast(m_parent, IValueFrame); }

	/**
	* Obtiene un hijo del objeto.
	*/
	IValueFrame* GetChild(unsigned int idx);
	IValueFrame* GetChild(unsigned int idx, const wxString& type);

	IValueFrame* FindNearAncestor(const wxString& type) { return wxDynamicCast(IObjectBase::FindNearAncestor(type), IValueFrame); }
	IValueFrame* FindNearAncestorByBaseClass(const wxString& type) { return wxDynamicCast(IObjectBase::FindNearAncestorByBaseClass(type), IValueFrame); }

	/**
	* Support generate id
	*/
	virtual form_identifier_t GenerateNewID();

	/**
	* Support get/set object id
	*/
	virtual bool SetControlID(const form_identifier_t &id) {
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
	virtual form_identifier_t GetControlID() const { return m_controlId; }

	bool SetControlName(const wxString& controlName) {
		IValueFrame* foundedControl =
			FindControlByName(controlName);
		wxASSERT(foundedControl == NULL);
		if (foundedControl == NULL) {
			Property* propName = GetProperty("name");
			if (propName != NULL) {
				propName->SetValue(controlName);
			}
			m_controlName = controlName;
			return true;
		}
		return false;
	}

	wxString GetControlName() const { return m_controlName; }

	void ResetGuid() { wxASSERT(m_controlGuid.isValid()); m_controlGuid.reset(); }
	void GenerateGuid() { wxASSERT(!m_controlGuid.isValid()); m_controlGuid = Guid::newGuid(); }
	Guid GetControlGuid() const { return m_controlGuid; }

	CLASS_ID GetClsid() const { return m_controlClsid; }
	void SetClsid(const CLASS_ID &clsid) { m_controlClsid = clsid; }

	/**
	* Find by control id
	*/
	virtual IValueFrame* FindControlByName(const wxString& controlName);
	virtual IValueFrame* FindControlByID(const form_identifier_t &id);

	/**
	* Support form
	*/
	virtual CValueForm* GetOwnerForm() const = 0;
	virtual void SetOwnerForm(CValueForm* ownerForm) = 0;

	/**
	* Support default menu
	*/
	virtual void PrepareDefaultMenu(wxMenu* m_menu) {}
	virtual void ExecuteMenu(IVisualHost* visualHost, int id) {}

	/**
	* Get wxObject from visual view (if exist)
	*/
	wxObject* GetWxObject();

	/**
	Sets whether the object is expanded in the object tree or not.
	*/
	void SetExpanded(bool expanded) { m_expanded = expanded; }

	/**
	Gets whether the object is expanded in the object tree or not.
	*/
	bool GetExpanded() { return m_expanded; }

	/**
	* Set property data
	*/
	virtual void SetPropertyData(Property* property, const CValue& srcValue);

	/**
	* Get property data
	*/
	virtual CValue GetPropertyData(Property* property);

	//get metadata
	virtual IMetadata* GetMetaData() const = 0;

	//get typelist 
	virtual OptionList* GetTypelist() const = 0;

	/**
	* Can delete object
	*/
	virtual bool CanDeleteControl() const = 0;

	// filter data 
	virtual bool FilterSource(const CSourceExplorer& src, const meta_identifier_t &id);

public:

	/**
	* Create an instance of the wxObject and return a pointer
	*/
	virtual wxObject* Create(wxObject* parent, IVisualHost* visualHost) { return new wxNoObject; }

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
	virtual bool CallEvent(const wxString& sEventName);
	virtual bool CallEvent(const wxString& sEventName, CValue& value1);
	virtual bool CallEvent(const wxString& sEventName, CValue& value1, CValue& value2);
	virtual bool CallEvent(const wxString& sEventName, CValue& value1, CValue& value2, CValue& value3);

	//call current form
	virtual void CallFunction(const wxString& functionName);
	virtual void CallFunction(const wxString& functionName, CValue& value1);
	virtual void CallFunction(const wxString& functionName, CValue& value1, CValue& value2);
	virtual void CallFunction(const wxString& functionName, CValue& value1, CValue& value2, CValue& value3);

public:

	virtual void ChoiceProcessing(CValue& vSelected) {}

public:

	//read&save propery 
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

	//support actions 
	virtual actionData_t GetActions(const form_identifier_t &formType) { return actionData_t(); }
	virtual void ExecuteAction(const action_identifier_t &action, CValueForm* srcForm) {}

	//is item 
	virtual bool IsItem() = 0;

	class CValueEventContainer : public CValue {

		CMethods* m_methods;
		IValueFrame* m_controlEvent;

	public:

		CValueEventContainer();
		CValueEventContainer(IValueFrame* ownerEvent);
		virtual ~CValueEventContainer();

		virtual CMethods* GetPMethods() const { PrepareNames(); return m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
		virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
		virtual CValue Method(methodArg_t& aParams);

		virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);
		virtual CValue GetAttribute(attributeArg_t& aParams); //значение атрибута

		virtual void SetAt(const CValue& cKey, CValue& cVal);
		virtual CValue GetAt(const CValue& cKey);

		virtual wxString GetTypeString() const { return wxT("events"); }
		virtual wxString GetString() const { return wxT("events"); }

		//Расширенные методы:
		bool Property(const CValue& cKey, CValue& cValueFound);
		unsigned int Count() const { return m_controlEvent->GetEventCount(); }

		//Работа с итераторами:
		virtual bool HasIterator() const { return true; }
		virtual CValue GetItEmpty();
		virtual CValue GetItAt(unsigned int idx);
		virtual unsigned int GetItSize() const { return Count(); }

	private:
		wxDECLARE_DYNAMIC_CLASS(CValueEventContainer);
	};

	virtual CValue GetControlValue() const { return CValue(); }

public:

	/**
	* Get type form
	*/
	virtual form_identifier_t GetTypeForm() const = 0;

	//runtime 
	virtual CProcUnit* GetFormProcUnit() const = 0;

	//methods 
	virtual CMethods* GetPMethods() const { PrepareNames();  return m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams);       //вызов метода

	//attributes 
	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t& aParams);                   //значение атрибута
	virtual int FindAttribute(const wxString& sName) const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//check is empty
	virtual inline bool IsEmpty() const override { return false; }

public:

	//load & save object in metaObject 
	bool LoadControl(IMetaFormObject* metaForm, CMemoryReader& dataReader);
	bool SaveControl(IMetaFormObject* metaForm, CMemoryWriter& dataWritter = CMemoryWriter());

protected:

	//load & save event in control 
	virtual bool LoadEvent(CMemoryReader& reader);
	virtual bool SaveEvent(CMemoryWriter& writer = CMemoryWriter());

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader) { return true; }
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter()) { return true; }

protected:

	bool m_expanded = true; // is expanded in the object tree, allows for saving to file

	CLASS_ID m_controlClsid; //type object name
	form_identifier_t m_controlId;
	Guid m_controlGuid;
	wxString m_controlName;
};

#endif // !_BASE_H_
