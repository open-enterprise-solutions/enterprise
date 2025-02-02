#ifndef __OBJECT_INSPECT__H__
#define __OBJECT_INSPECT__H__

#include "backend/wrapper/propertyInfo.h"

#include <wx/aui/auibook.h>
#include <wx/propgrid/manager.h>

#if !wxUSE_PROPGRID
#error "wxUSE_PROPGRID must be set to 1 in your wxWidgets library."
#endif

#include "frontend/frontend.h"
#include "frontend/mainFrame/objinspect/advprops.h"

static int wxEVT_OES_PROP_BITMAP_CHANGED = wxNewEventType();

enum {
	wxOES_OI_DEFAULT_STYLE,
	wxOES_OI_MULTIPAGE_STYLE,
	wxOES_OI_SINGLE_PAGE_STYLE
};

#define objectInspector  CObjectInspector::GetObjectInspector()

class FRONTEND_API CObjectInspector : public wxPanel {

	wxPropertyGridManager* m_pg;

	std::map< wxString, bool > m_isExpanded;

	std::map< wxPGProperty*, Property*> m_propMap;
	std::map< wxPGProperty*, Event*> m_eventMap;

	IPropertyObject* m_currentSel;

	int m_style;

	//save the current selected property
	wxString m_strSelPropItem;

private:

	int StringToBits(const wxString& strVal, wxPGChoices& constants) {
		wxStringTokenizer strTok(strVal, wxT(" |"));
		int val = 0;
		while (strTok.HasMoreTokens())
		{
			wxString token = strTok.GetNextToken();
			unsigned int i = 0;
			bool done = false;
			while (i < constants.GetCount() && !done)
			{
				if (constants.GetLabel(i) == token)
				{
					val |= constants.GetValue(i);
					done = true;
				}
				i++;
			}
		}
		return val;
	}

	template < class ValueT >
	void CreateCategory(const wxString& name, IPropertyObject* obj, std::map< wxString, ValueT >& itemMap, bool addingEvents) {
		// Get Category
		PropertyCategory* category = obj->GetCategory();
		if (category == nullptr)
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
		if (pageIndex == wxNOT_FOUND) {
			//pg->AddPage( pageName, obj_info->GetSmallIconFile() );
			pg->AddPage(pageName);
		}
		const wxString& catName = category->GetName();
		wxPGProperty* id = pg->Append(new wxPropertyCategory(category->GetLabel(), catName));
		AddItems(name, obj, category, itemMap);
		std::map< wxString, bool >::iterator it = m_isExpanded.find(catName);
		if (it != m_isExpanded.end()) {
			if (it->second) {
				pg->Expand(id);
			}
			else {
				pg->Collapse(id);
			}
		}
		pg->SetPropertyAttributeAll(wxPG_BOOL_USE_CHECKBOX, (long)1);
	}

	void GenerateSynonym(const wxString& name);

	void AddItems(const wxString& name, IPropertyObject* obj, PropertyCategory* category, std::map<wxString, Property*>& properties) {
		unsigned int propCount = category->GetPropertyCount();
		for (unsigned int i = 0; i < propCount; i++) {

			wxString strPropName = category->GetPropertyName(i);
			Property* prop = obj->GetProperty(strPropName);

			if (!prop)
				continue;

			// we do not want to duplicate inherited properties
			if (properties.find(strPropName) == properties.end()) {

				if (m_currentSel != nullptr) {
					m_currentSel->OnPropertyCreated(prop);
				}

				wxPGProperty* id = m_pg->Append(GetProperty(prop));
				int           propType = prop->GetType();
				if (propType != PropertyType::PT_OPTION &&
					propType != PropertyType::PT_TYPE &&
					propType != PropertyType::PT_RECORD &&
					propType != PropertyType::PT_GENERATION &&
					propType != PropertyType::PT_OWNER) {
					m_pg->SetPropertyHelpString(id, prop->GetHelp());
					if (propType == PropertyType::PT_BITMAP) {
						wxPGBitmapProperty* bp = wxDynamicCast(id, wxPGBitmapProperty);
						if (bp != nullptr) {
							bp->CreateChildren();
							// perform delayed child properties update
							wxCommandEvent e(wxEVT_OES_PROP_BITMAP_CHANGED);
							e.SetString(bp->GetName() + wxT(":") + prop->GetValueAsString());
							GetEventHandler()->AddPendingEvent(e);
						}
					}
				}

				if (m_style != wxOES_OI_MULTIPAGE_STYLE) {
					// Most common classes will be showed with a slightly different colour.
					if (name == wxT("Window"))
						m_pg->SetPropertyBackgroundColour(id, wxColour(255, 255, 205)); // yellow
					else if (name == wxT("Common"))
						m_pg->SetPropertyBackgroundColour(id, wxColour(240, 240, 255)); // light blue
					else if (name == wxT("SizerItem"))
						m_pg->SetPropertyBackgroundColour(id, wxColour(220, 255, 255)); // cyan
				}

				std::map< wxString, bool >::iterator it = m_isExpanded.find(strPropName);
				if (it != m_isExpanded.end()) {
					if (it->second) {
						m_pg->Expand(id);
					}
					else {
						m_pg->Collapse(id);
					}
				}

				if (prop->GetType() == PropertyType::PT_TYPE
					|| prop->GetType() == PropertyType::PT_SOURCE) {
					m_pg->Expand(id);
				}

				if (m_currentSel != nullptr) {
					m_currentSel->OnPropertyCreated(prop);
				}

				properties.insert(std::map<wxString, Property*>::value_type(strPropName, prop));
				m_propMap.insert(std::map< wxPGProperty*, Property*>::value_type(id, prop));
			}
		}

		unsigned int catCount = category->GetCategoryCount();
		for (unsigned int i = 0; i < catCount; i++) {

			PropertyCategory* nextCat = category->GetCategory(i);
			if (0 == nextCat->GetCategoryCount() && 0 == nextCat->GetPropertyCount()) {
				continue;
			}

			wxPGProperty* catId = m_pg->AppendIn(category->GetName(), new wxPropertyCategory(nextCat->GetLabel(), nextCat->GetName()));
			AddItems(nextCat->GetName(), obj, nextCat, properties);

			std::map< wxString, bool >::iterator it = m_isExpanded.find(nextCat->GetName());
			if (it != m_isExpanded.end())
			{
				if (it->second) {
					m_pg->Expand(catId);
				}
				else {
					m_pg->Collapse(catId);
				}
			}
		}
	}

	void AddItems(const wxString& name, IPropertyObject* obj, PropertyCategory* category, std::map<wxString, Event*>& events) {
		unsigned int eventCount = category->GetEventCount();
		for (unsigned int i = 0; i < eventCount; i++) {

			wxString eventName = category->GetEventName(i);
			Event* event = obj->GetEvent(eventName);

			if (!event)
				continue;

			// We do not want to duplicate inherited events
			if (events.find(eventName) == events.end()) {

				wxPGProperty* id = m_pg->Append(GetEvent(event));

				m_pg->SetPropertyHelpString(id, wxGetTranslation(event->GetHelp()));

				if (m_style != wxOES_OI_MULTIPAGE_STYLE) {
					// Most common classes will be showed with a slightly different colour.
					if (name == wxT("Window"))
						m_pg->SetPropertyBackgroundColour(id, wxColour(255, 255, 205)); // Yellow
					else if (name == wxT("Common"))
						m_pg->SetPropertyBackgroundColour(id, wxColour(240, 240, 255)); // light blue
					else if (name == wxT("SizerItem"))
						m_pg->SetPropertyBackgroundColour(id, wxColour(220, 255, 255)); // Cyan
				}

				std::map< wxString, bool >::iterator it = m_isExpanded.find(eventName);
				if (it != m_isExpanded.end()) {
					if (it->second) {
						m_pg->Expand(id);
					}
					else {
						m_pg->Collapse(id);
					}
				}

				events.insert(std::map<wxString, Event*>::value_type(eventName, event));
				m_eventMap.insert(std::map< wxPGProperty*, Event*>::value_type(id, event));
			}
		}

		unsigned int catCount = category->GetCategoryCount();
		for (unsigned int i = 0; i < catCount; i++)
		{
			PropertyCategory* nextCat = category->GetCategory(i);
			if (0 == nextCat->GetCategoryCount() && 0 == nextCat->GetEventCount()) {
				continue;
			}

			wxPGProperty* catId = m_pg->AppendIn(category->GetName(), new wxPropertyCategory(nextCat->GetLabel(), nextCat->GetName()));

			AddItems(nextCat->GetName(), obj, nextCat, events);

			std::map< wxString, bool >::iterator it = m_isExpanded.find(nextCat->GetName());
			if (it != m_isExpanded.end()) {
				if (it->second) {
					m_pg->Expand(catId);
				}
				else {
					m_pg->Collapse(catId);
				}
			}
		}
	}

	friend class CDocMDIFrame; 

	wxPGProperty* GetProperty(Property* prop);
	wxPGProperty* GetEvent(Event* event);

	void Create(bool force = false);

	bool ModifyProperty(Property* prop, const wxVariant& newValue);
	bool ModifyEvent(Event* event, const wxVariant& newValue);

	void RestoreLastSelectedPropItem() {
		wxPGProperty* p = m_pg->GetPropertyByName(wxT("name"));
		if (p != nullptr)
			m_pg->SelectProperty(p, true);
	}

	CObjectInspector(wxWindow* parent, int id, int style = wxOES_OI_DEFAULT_STYLE);

public:

	virtual ~CObjectInspector();

	wxPropertyGridManager* CreatePropertyGridManager(wxWindow* parent, wxWindowID id);

	// Servicios para los observadores
	void SelectObject(IPropertyObject* selobj, bool force = true) {
		ShowProperty();
		m_currentSel = selobj;
		Create(force);
	}

	IPropertyObject* GetSelectedObject() const {
		return m_currentSel;
	}

	void ShowProperty();
	void SavePosition();
	void ClearProperty();

	static CObjectInspector* GetObjectInspector();

protected:

	void OnPropertyGridChanging(wxPropertyGridEvent& event);
	void OnPropertyGridChanged(wxPropertyGridEvent& event);
	void OnPropertyGridExpand(wxPropertyGridEvent& event);
	void OnPropertyGridItemSelected(wxPropertyGridEvent& event);

	void OnBitmapPropertyChanged(wxCommandEvent& event);
	void OnChildFocus(wxChildFocusEvent& event);

	wxDECLARE_EVENT_TABLE();
};

#endif //__OBJ_INSPECT__