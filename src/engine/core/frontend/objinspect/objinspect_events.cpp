#include "objinspect.h"
#include "frontend/mainFrame.h"

void CObjectInspector::SelectObject(IObjectBase *selobj, bool force)
{
	ShowProperty();

	m_currentSel = selobj;
	m_evthandler = NULL;

	Create(force);
}

void CObjectInspector::SelectObject(IObjectBase *selobj, wxEvtHandler *handler, bool force)
{
	ShowProperty();

	m_currentSel = selobj;
	m_evthandler = handler;

	Create(force);
}

void CObjectInspector::ShowProperty()
{
	mainFrame->ShowProperty();
}

void CObjectInspector::RefreshProperty()
{
	m_pg->Freeze();

	int pageNumber = m_pg->GetSelectedPage();
	wxString pageName;
	if (pageNumber != wxNOT_FOUND)
	{
		pageName = m_pg->GetPageName(pageNumber);
	}

	m_currentSel->OnPropertyCreated();

	wxPGProperty *lastProperty = m_pg->GetSelectedProperty();
	wxString lastPropName = lastProperty->GetName();

	// Clear Property Grid Manager
	m_pg->Clear();

	m_propMap.clear();
	m_eventMap.clear();

	std::map<wxString, Property *> propMap, dummyPropMap;
	std::map<wxString, Event *> eventMap, dummyEventMap;

	// We create the categories with the properties of the object organized by "classes"
	CreateCategory(m_currentSel->GetClassName(), m_currentSel, propMap, false);

	IObjectBase* parent = m_currentSel->GetParent();
	if (parent) {
		if (parent->IsItem()) {
			CreateCategory(m_currentSel->GetClassName(), parent, dummyPropMap, false);
		}
	}

	CreateCategory(m_currentSel->GetClassName(), m_currentSel, eventMap, true);

	if (parent)
	{
		if (parent->IsItem()) {
			CreateCategory(m_currentSel->GetClassName(), parent, dummyEventMap, true);
		}
	}

	// Select previously selected page, or first page
	if (m_pg->GetPageCount() > 0)
	{
		int pageIndex = m_pg->GetPageByName(pageName);
		if (wxNOT_FOUND != pageIndex) {
			m_pg->SelectPage(pageIndex);
		}
		else {
			m_pg->SelectPage(0);
		}
	}

	m_pg->Refresh();
	m_pg->Update();

	m_pg->Thaw();

	RestoreLastSelectedPropItem();
}

IObjectBase* CObjectInspector::GetSelectedObject()
{
	return m_currentSel;
}