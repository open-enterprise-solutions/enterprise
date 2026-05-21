#ifndef __OBJECT_INSPECT_H__
#define __OBJECT_INSPECT_H__

#include "backend/propertyManager/propertyManager.h"

#include <wx/aui/auibook.h>
#include <wx/propgrid/manager.h>
#include <wx/config.h>

#if !wxUSE_PROPGRID
#error "wxUSE_PROPGRID must be set to 1 in your wxWidgets library."
#endif

#include "frontend/frontend.h"

static int wxEVT_OES_PROP_PICTURE_CHANGED = wxNewEventType();

enum {
	wxOES_OI_DEFAULT_STYLE,
	wxOES_OI_MULTIPAGE_STYLE,
	wxOES_OI_SINGLE_PAGE_STYLE
};

#define objectInspector  ibObjectInspector::GetObjectInspector()

class FRONTEND_API ibObjectInspector final : public wxPanel {

	ibObjectInspector(wxWindow* parent, int id, int style = wxOES_OI_DEFAULT_STYLE);

public:

	virtual ~ibObjectInspector();

	wxPropertyGridManager* GetPropertyManager() const { return m_pg; }

	// Hides or reveals a property.
	// hide - If true, hides property, otherwise reveals it.
	// flags - By default changes are applied recursively. Set this parameter
	//   wxPG_DONT_RECURSE to prevent this.
	bool HideProperty(wxPGPropArg id,
		bool hide = true,
		wxPGPropertyValuesFlags flags = wxPGPropertyValuesFlags::Recurse) { return m_pg->HideProperty(id, hide, flags); }

	bool ShowProperty(wxPGPropArg id,
		bool show = true,
		wxPGPropertyValuesFlags flags = wxPGPropertyValuesFlags::Recurse) {
		return m_pg->HideProperty(id, !show, flags);
	}

	wxPGProperty* GetProperty(ibProperty* prop) const;
	wxPGProperty* GetEvent(ibEvent* event) const;

	// Servicios para los observadores
	void SelectObject(ibPropertyObject* selobj, bool force = false) {

		if (IsShownInspector() && (force || m_currentSel != selobj)) {
			Create(selobj, force);
		}

		if (selobj != nullptr)
			wxLogDebug(wxT("! <debug> activate property %s"), selobj->GetClassName());
		else
			wxLogDebug(wxT("! <debug> clear property"));
	}

	ibPropertyObject* GetSelectedObject() const { return m_currentSel; }

	bool IsShownInspector() const;
	void ShowInspector();

	void SavePosition();

	static ibObjectInspector* GetObjectInspector();

protected:

	void OnPropertyGridChanging(wxPropertyGridEvent& event);
	void OnPropertyGridChanged(wxPropertyGridEvent& event);
	void OnPropertyGridExpand(wxPropertyGridEvent& event);
	void OnPropertyGridItemSelected(wxPropertyGridEvent& event);

	// Workmate parity — right-click on a "Synonym/Comment/Tooltip/Title/
	// Caption" cell pops a "Сгенерировать через AI" context menu and
	// dispatches ibAIPropertyHelper::RunGenerate. Non-text properties
	// fall through to the default wxPG behaviour.
	void OnPropertyGridRightClick(wxPropertyGridEvent& event);

	void OnBitmapPropertyChanged(wxCommandEvent& event);
	void OnChildFocus(wxChildFocusEvent& event);

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
	void CreateCategory(const wxString& name, ibPropertyObject* obj, std::map< wxString, ValueT >& itemMap, bool addingEvents) {
		// Get Category
		ibPropertyCategory* category = obj->GetCategory();
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
		if (!stringUtils::CompareString(catName, propertyDefName)) {
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
		}
		else {
			AddItems(name, obj, category, itemMap);
		}

		pg->SetPropertyAttributeAll(wxPG_BOOL_USE_CHECKBOX, (long)1);
	}

	void AddItems(const wxString& name, ibPropertyObject* obj, ibPropertyCategory* category, std::map<wxString, ibProperty*>& properties) {
		unsigned int propCount = category->GetPropertyCount();
		for (unsigned int i = 0; i < propCount; i++) {

			const wxString& strPropName = category->GetPropertyName(i);
			ibProperty* prop = obj->GetProperty(strPropName);

			if (!prop)
				continue;

			// we do not want to duplicate inherited properties
			if (properties.find(strPropName) == properties.end()) {

				if (m_currentSel != nullptr) {
					m_currentSel->OnPropertyCreated(prop);
				}
				wxPGProperty* pg = GetProperty(prop);
				if (pg != nullptr) {
					wxPGProperty* id = m_pg->Append(pg);
					if (m_style != wxOES_OI_MULTIPAGE_STYLE) {
						// Most common classes will be showed with a slightly different colour.
						if (stringUtils::CompareString(name, wxT("window")))
							m_pg->SetPropertyBackgroundColour(id, wxColour(255, 255, 205)); // yellow
						else if (stringUtils::CompareString(name, wxT("common")))
							m_pg->SetPropertyBackgroundColour(id, wxColour(240, 240, 255)); // light blue
						else if (stringUtils::CompareString(name, wxT("sizerItem")))
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

					id->RefreshChildren();

					properties.insert(std::map<wxString, ibProperty*>::value_type(strPropName, prop));
					m_propMap.insert(std::map< wxPGProperty*, ibProperty*>::value_type(id, prop));
				}

				if (m_currentSel != nullptr) {
					m_currentSel->OnPropertyCreated(prop);
				}
			}
		}

		unsigned int catCount = category->GetCategoryCount();
		for (unsigned int i = 0; i < catCount; i++) {

			ibPropertyCategory* nextCat = category->GetCategory(i);
			if (0 == nextCat->GetCategoryCount() && 0 == nextCat->GetPropertyCount()) {
				continue;
			}

			const wxString& catName = category->GetName();
			if (!stringUtils::CompareString(catName, propertyDefName)) {
				wxPGProperty* catId = m_pg->AppendIn(catName, new wxPropertyCategory(nextCat->GetLabel(), nextCat->GetName()));
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
			else {
				wxPGProperty* catId = m_pg->Append(new wxPropertyCategory(nextCat->GetLabel(), nextCat->GetName()));
				AddItems(nextCat->GetName(), obj, nextCat, properties);
			}
		}
	}

	void AddItems(const wxString& name, ibPropertyObject* obj, ibPropertyCategory* category, std::map<wxString, ibEvent*>& events) {
		unsigned int eventCount = category->GetEventCount();
		for (unsigned int i = 0; i < eventCount; i++) {

			const wxString& eventName = category->GetEventName(i);
			ibEvent* event = obj->GetEvent(eventName);

			if (!event)
				continue;

			// We do not want to duplicate inherited events
			if (events.find(eventName) == events.end()) {
				wxPGProperty* eg = GetEvent(event);
				if (eg != nullptr) {
					wxPGProperty* id = m_pg->Append(eg);
					m_pg->SetPropertyHelpString(id, wxGetTranslation(event->GetHelp()));
					if (m_style != wxOES_OI_MULTIPAGE_STYLE) {
						// Most common classes will be showed with a slightly different colour.
						if (stringUtils::CompareString(name, wxT("window")))
							m_pg->SetPropertyBackgroundColour(id, wxColour(255, 255, 205)); // yellow
						else if (stringUtils::CompareString(name, wxT("common")))
							m_pg->SetPropertyBackgroundColour(id, wxColour(240, 240, 255)); // light blue
						else if (stringUtils::CompareString(name, wxT("sizerItem")))
							m_pg->SetPropertyBackgroundColour(id, wxColour(220, 255, 255)); // cyan
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
					events.insert(std::map<wxString, ibEvent*>::value_type(eventName, event));
					m_eventMap.insert(std::map< wxPGProperty*, ibEvent*>::value_type(id, event));
				}
			}
		}

		unsigned int catCount = category->GetCategoryCount();
		for (unsigned int i = 0; i < catCount; i++)
		{
			ibPropertyCategory* nextCat = category->GetCategory(i);
			if (0 == nextCat->GetCategoryCount() && 0 == nextCat->GetEventCount()) {
				continue;
			}

			const wxString& catName = category->GetName();
			if (!stringUtils::CompareString(catName, propertyDefName)) {
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
			else {
				wxPGProperty* catId = m_pg->Append(new wxPropertyCategory(nextCat->GetLabel(), nextCat->GetName()));
				AddItems(nextCat->GetName(), obj, nextCat, events);
			}
		}
	}

	friend class ibFrontendDocMDIFrame;

	wxPropertyGridManager* CreatePropertyGridManager(wxWindow* parent, wxWindowID id) const;

	void Create(ibPropertyObject* object, bool force = false);

	bool ModifyProperty(ibProperty* prop, const wxVariant& newValue);
	bool ModifyEvent(ibEvent* event, const wxVariant& newValue);

	void RestoreLastSelectedPropItem() {

		wxPGProperty* propPtr = m_pg->GetPropertyByName(wxT("name"));
		if (propPtr != nullptr) {

			m_pg->SelectProperty(propPtr, false);

			std::map< wxPGProperty*, ibProperty*>::iterator itProperty = m_propMap.find(propPtr);
			if (itProperty != m_propMap.end()) {
				ibProperty* prop_ptr = itProperty->second;
				// Update displayed description for the new selection
				const wxString& helpString = prop_ptr->GetHelp();
				const wxString& localized = wxGetTranslation(helpString);
				m_pg->SetPropertyHelpString(propPtr, localized);
				m_pg->SetDescription(propPtr->GetLabel(), localized);
				return;
			}

			std::map< wxPGProperty*, ibEvent*>::iterator itEvent = m_eventMap.find(propPtr);
			if (itEvent != m_eventMap.end()) {
				ibEvent* event_ptr = itEvent->second;
				// Update displayed description for the new selection
				const wxString& helpString = event_ptr->GetHelp();
				const wxString& localized = wxGetTranslation(helpString);
				m_pg->SetPropertyHelpString(propPtr, localized);
				m_pg->SetDescription(propPtr->GetLabel(), localized);
				return;
			}
		}

		m_pg->SetDescription(wxEmptyString, wxEmptyString);
	}

	wxPropertyGridManager* m_pg;

	std::map< wxString, bool > m_isExpanded;

	std::map< wxPGProperty*, ibProperty*> m_propMap;
	std::map< wxPGProperty*, ibEvent*> m_eventMap;

	ibPropertyObject* m_currentSel;

	int m_style;

	//save the current selected property
	wxString m_strSelPropItem;

	wxDECLARE_EVENT_TABLE();
};

#endif //__OBJ_INSPECT__