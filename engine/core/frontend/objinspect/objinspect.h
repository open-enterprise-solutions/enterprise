#ifndef __OBJ_INSPECT__
#define __OBJ_INSPECT__

#include "common/objectbase.h"

#include <wx/aui/auibook.h>
#include <wx/propgrid/manager.h>

#if !wxUSE_PROPGRID
#error "wxUSE_PROPGRID must be set to 1 in your wxWidgets library."
#endif

class wxFrameEventHandlerEvent;
class wxFramePropertyEvent;
class wxFrameObjectEvent;
class wxFrameEvent;

enum {
	wxOES_OI_DEFAULT_STYLE,
	wxOES_OI_MULTIPAGE_STYLE,
	wxOES_OI_SINGLE_PAGE_STYLE
};

class CObjectInspector;

#define objectInspector           (CObjectInspector::Get())
#define objectInspectorDestroy()  (CObjectInspector::Destroy())

class CObjectInspector : public wxPanel
{
	static CObjectInspector *s_instance;

private:

	std::map< wxPGProperty*, Property*> m_propMap;
	std::map< wxPGProperty*, Event*> m_eventMap;

	IObjectBase *m_currentSel;
	wxEvtHandler *m_evthandler;

	//save the current selected property
	wxString m_strSelPropItem;

	wxPropertyGridManager* m_pg;

	int m_style;

	int StringToBits(const wxString& strVal, wxPGChoices& constants);

	std::map< wxString, bool > m_isExpanded;

	template < class ValueT >
	void CreateCategory(const wxString& name, IObjectBase* obj, std::map< wxString, ValueT >& itemMap, bool addingEvents)
	{
		// Get Category
		IObjectBase::PropertyContainer* category = obj->GetCategory();

		if (!category) 
			return;

		// Prevent page creation if there are no properties
		if (category->GetCategoryCount() == 0 && 
			(addingEvents ? category->GetEventCount() : category->GetPropertyCount()) == 0) 
			return;

		wxString pageName;

		if (m_style == wxOES_OI_MULTIPAGE_STYLE) {
			pageName = name; 
		}
		else {
			pageName = wxT("default");
		}

		wxPropertyGridManager* pg = m_pg; //(addingEvents ? m_eg : m_pg);
		int pageIndex = pg->GetPageByName(pageName);

		if (pageIndex == wxNOT_FOUND)
		{
			//pg->AddPage( pageName, obj_info->GetSmallIconFile() );
			pg->AddPage(pageName);
		}

		const wxString& catName = category->GetName();
		wxPGProperty* id = pg->Append(new wxPropertyCategory(category->GetDescription(), catName));

		AddItems(name, obj, category, itemMap);

		std::map< wxString, bool >::iterator it = m_isExpanded.find(catName);

		if (it != m_isExpanded.end())
		{
			if (it->second) {
				pg->Expand(id);
			}
			else {
				pg->Collapse(id);
			}
		}

		pg->SetPropertyAttributeAll(wxPG_BOOL_USE_CHECKBOX, (long)1);
	}

	void GenerateSynonym(const wxString &name);

	void AddItems(const wxString& name, IObjectBase* obj, IObjectBase::PropertyContainer* category, std::map<wxString, Property *>& map);
	void AddItems(const wxString& name, IObjectBase* obj, IObjectBase::PropertyContainer* category, std::map<wxString, Event *>& map);

	wxPGProperty* GetProperty(Property* prop);

	void Create(bool force = false);

	void OnPropertyGridChanging(wxPropertyGridEvent& event);
	void OnPropertyGridExpand(wxPropertyGridEvent& event);
	void OnPropertyGridItemSelected(wxPropertyGridEvent& event);

	void OnBitmapPropertyChanged(wxCommandEvent& event);

	bool ModifyProperty(Property* prop, const wxString& str);
	void RestoreLastSelectedPropItem();

	void OnChildFocus(wxChildFocusEvent& event);

	CObjectInspector(wxWindow *parent, int id, int style = wxOES_OI_DEFAULT_STYLE);

public:

	virtual ~CObjectInspector();

	static CObjectInspector* Get();
	static void Destroy();

	void OnObjectSelected(wxFrameObjectEvent& event);
	void OnProjectRefresh(wxFrameEvent& event);
	void OnPropertyModified(wxFramePropertyEvent& event);

	// Servicios para los observadores
	void SelectObject(IObjectBase *selobj, bool force = true);
	void SelectObject(IObjectBase *selobj, wxEvtHandler *m_handler, bool force = true);
	IObjectBase* GetSelectedObject();

	void ShowProperty();
	void RefreshProperty();

	wxPropertyGridManager* CreatePropertyGridManager(wxWindow *parent, wxWindowID id);
	void SavePosition();

	void ClearProperty();

	wxDECLARE_EVENT_TABLE(); 
};

#endif //__OBJ_INSPECT__