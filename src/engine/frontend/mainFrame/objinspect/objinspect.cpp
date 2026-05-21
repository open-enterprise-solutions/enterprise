#include "objinspect.h"

#include "frontend/propertyManager/aiPropertyHelper.h"

enum {
	WXOES_PROPERTY_GRID = wxID_HIGHEST + 1000
};

// -----------------------------------------------------------------------
// ibObjectInspector
// -----------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibObjectInspector, wxPanel)
EVT_PG_CHANGING(WXOES_PROPERTY_GRID, ibObjectInspector::OnPropertyGridChanging)
EVT_PG_CHANGED(WXOES_PROPERTY_GRID, ibObjectInspector::OnPropertyGridChanged)
EVT_PG_ITEM_COLLAPSED(WXOES_PROPERTY_GRID, ibObjectInspector::OnPropertyGridExpand)
EVT_PG_ITEM_EXPANDED(WXOES_PROPERTY_GRID, ibObjectInspector::OnPropertyGridExpand)
EVT_PG_SELECTED(WXOES_PROPERTY_GRID, ibObjectInspector::OnPropertyGridItemSelected)
EVT_PG_RIGHT_CLICK(WXOES_PROPERTY_GRID, ibObjectInspector::OnPropertyGridRightClick)
EVT_CHILD_FOCUS(ibObjectInspector::OnChildFocus)
wxEND_EVENT_TABLE()

///////////////////////////////////////////////////////////////////////////////
// ibObjectInspector
///////////////////////////////////////////////////////////////////////////////

ibObjectInspector::ibObjectInspector(wxWindow* parent, int id, int style)
	: wxPanel(parent, id), m_style(style), m_currentSel(nullptr)
{
	m_pg = CreatePropertyGridManager(this, WXOES_PROPERTY_GRID);

	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
	topSizer->Add(m_pg, 1, wxALL | wxEXPAND, 0);
	SetSizer(topSizer);

	ibObjectInspector::Connect(wxID_ANY, wxEVT_OES_PROP_PICTURE_CHANGED, wxCommandEventHandler(ibObjectInspector::OnBitmapPropertyChanged));
}

ibObjectInspector::~ibObjectInspector()
{
	ibObjectInspector::Disconnect(wxID_ANY, wxEVT_OES_PROP_PICTURE_CHANGED, wxCommandEventHandler(ibObjectInspector::OnBitmapPropertyChanged));
}

///////////////////////////////////////////////////////////////////////////////

void ibObjectInspector::SavePosition()
{
	// Save Layout
	wxConfigBase* config = wxConfigBase::Get();
	config->Write(wxT("/mainFrame/objectInspector/DescBoxHeight"), m_pg->GetDescBoxHeight());
}

#include "frontend/mainFrame/mainFrame.h"

ibObjectInspector* ibObjectInspector::GetObjectInspector()
{
	return ibFrontendDocMDIFrame::GetObjectInspector();
}

#include "frontend/visualView/formdefs.h"

void ibObjectInspector::Create(ibPropertyObject* object, bool force)
{
	if (force || object != m_currentSel) {
		m_pg->Freeze();
		m_currentSel = object;

		const int pageNumber = m_pg->GetSelectedPage();

		wxString pageName;
		if (pageNumber != wxNOT_FOUND) {
			pageName = m_pg->GetPageName(pageNumber);
		}

		// Clear Property Grid Manager
		m_pg->Clear();

		m_propMap.clear();
		m_eventMap.clear();

		if (object != nullptr) {

			std::map<wxString, ibProperty*> propMap, dummyPropMap;
			std::map<wxString, ibEvent*> eventMap, dummyEventMap;

			// We create the categories with the properties of the object organized by "classes"
			CreateCategory(object->GetClassName(), object, propMap, false);

			ibPropertyObject* owner = object->GetOwner();

			if (owner != nullptr) {
				if (owner->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
					CreateCategory(object->GetClassName(), owner, dummyPropMap, false);
				}
			}

			CreateCategory(object->GetClassName(), object, eventMap, true);

			if (owner != nullptr) {
				if (owner->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
					CreateCategory(object->GetClassName(), owner, dummyEventMap, true);
				}
			}

			// Select previously object page, or first page
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

			m_currentSel->OnPropertyCreated();
		}

		m_pg->Refresh();
		m_pg->Update();

		m_pg->Thaw();
	}

	if (m_currentSel != nullptr) {
		m_pg->Freeze();
		for (auto& prop : m_propMap)
			m_currentSel->OnPropertyRefresh(m_pg, prop.first, prop.second);
		for (auto event : m_eventMap)
			m_currentSel->OnEventRefresh(m_pg, event.first, event.second);
		for (auto prop : m_propMap) {
			wxPGProperty* property = prop.first;
			if (property != nullptr) {
				wxPGProperty* parentProperty = property->GetParent();
				if (parentProperty->IsCategory() &&
					parentProperty->IsVisible() != (!property->HasFlag(wxPGFlags::Hidden))) {
					bool visible = false;
					for (unsigned int idx = 0; idx < parentProperty->GetChildCount(); idx++) {
						wxPGProperty* currChild = parentProperty->Item(idx);
						wxASSERT(currChild);
						if (!currChild->HasFlag(wxPGFlags::Hidden))
							visible = true;
					}
					if (parentProperty->IsVisible() != visible)
						parentProperty->Hide(!visible);
				}
			}
		}
		m_pg->Thaw();
	}

	RestoreLastSelectedPropItem();
}

bool ibObjectInspector::IsShownInspector() const
{
	if (mainFrame != nullptr)
		return mainFrame->IsShownInspector();
	return false;
}

void ibObjectInspector::ShowInspector()
{
	if (mainFrame != nullptr)
		mainFrame->ShowInspector();
}

#include "frontend/visualView/ctrl/frame.h"

wxPropertyGridManager* ibObjectInspector::CreatePropertyGridManager(wxWindow* parent, wxWindowID id) const
{
	int pgStyle;
	int defaultDescBoxHeight;

	switch (m_style) {
	case wxOES_OI_MULTIPAGE_STYLE:
		pgStyle = wxPG_BOLD_MODIFIED | wxPG_SPLITTER_AUTO_CENTER | wxPG_TOOLBAR | wxPG_DESCRIPTION | wxPG_TOOLTIPS | wxPGMAN_DEFAULT_STYLE;
		defaultDescBoxHeight = 50;
		break;
	case wxOES_OI_DEFAULT_STYLE:
	case wxOES_OI_SINGLE_PAGE_STYLE:
	default:
		pgStyle = wxPG_BOLD_MODIFIED | wxPG_SPLITTER_AUTO_CENTER | wxPG_DESCRIPTION| wxPG_TOOLTIPS | wxPGMAN_DEFAULT_STYLE;
		defaultDescBoxHeight = 150;
		break;
	}

	int descBoxHeight;
	wxConfigBase* config = wxConfigBase::Get();
	config->Read(wxT("/mainFrame/objectInspector/DescBoxHeight"), &descBoxHeight, defaultDescBoxHeight);
	if (descBoxHeight == wxNOT_FOUND) {
		descBoxHeight = defaultDescBoxHeight;
	}

	wxPropertyGridManager* pg = new wxPropertyGridManager(parent, id, wxDefaultPosition, wxDefaultSize, pgStyle);
	pg->SetExtraStyle(wxPG_EX_NATIVE_DOUBLE_BUFFERING);
	pg->SetDescBoxHeight(descBoxHeight);
	pg->SendSizeEvent();

	pg->SetForegroundColour(wxDefaultStypeFGColour);
	pg->SetBackgroundColour(wxDefaultStypeBGColour);

	pg->GetGrid()->SetMarginColour(wxDefaultStypeBGColour.ChangeLightness(95));
	
	pg->GetGrid()->SetCaptionBackgroundColour(wxDefaultStypeBGColour.ChangeLightness(95));

	pg->GetGrid()->SetCaptionTextColour(*wxBLACK);
	pg->GetGrid()->SetCellDisabledTextColour(*wxBLACK);

	pg->GetGrid()->SetCellTextColour(*wxBLACK);

	return pg;
}

wxPGProperty* ibObjectInspector::GetProperty(ibProperty* prop) const 
{
	wxPGProperty* result = (wxPGProperty* )prop->GetPGProperty();
	if (result != nullptr) {
		result->SetHelpString(prop->GetHelp());
		result->Enable(prop->IsEditable());
	}
	return result;
}

wxPGProperty* ibObjectInspector::GetEvent(ibEvent* event) const
{
	wxPGProperty* result = (wxPGProperty*)event->GetPGProperty();
	if (result != nullptr) {
		result->SetHelpString(event->GetHelp());
		result->Enable(event->IsEditable());
	}
	return result;
}

bool ibObjectInspector::ModifyProperty(ibProperty* prop, const wxVariant& newValue)
{
	const wxVariant oldValue = prop->GetValue();
	if (m_currentSel->OnPropertyChanging(prop, newValue)) {
		prop->SetValue(newValue);
		m_currentSel->OnPropertyChanged(prop, oldValue, newValue);
		return true;
	}
	return false;
}

bool ibObjectInspector::ModifyEvent(ibEvent* event, const wxVariant& newValue)
{
	const wxVariant oldValue = event->GetValue();
	if (m_currentSel->OnEventChanging(event, newValue)) {
		event->SetValue(newValue);
		m_currentSel->OnEventChanged(event, oldValue, newValue);
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////

void ibObjectInspector::OnPropertyGridChanging(wxPropertyGridEvent& event)
{
	wxPGProperty* propPtr = event.GetProperty();
	std::map< wxPGProperty*, ibProperty*>::iterator itProperty = m_propMap.find(propPtr);
	if (itProperty != m_propMap.end()) {
		ibProperty* prop_ptr = itProperty->second;
		// Update displayed description for the new selection
		const wxString& helpString = prop_ptr->GetHelp();
		if (!ModifyProperty(prop_ptr, event.GetPropertyValue()))
			event.Veto();
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
		if (!ModifyEvent(event_ptr, event.GetPropertyValue()))
			event.Veto();
		const wxString& localized = wxGetTranslation(helpString);
		m_pg->SetPropertyHelpString(propPtr, localized);
		m_pg->SetDescription(propPtr->GetLabel(), localized);
		return;
	}

	m_pg->SetPropertyHelpString(propPtr, wxEmptyString);
	m_pg->SetDescription(wxEmptyString, wxEmptyString);
}

void ibObjectInspector::OnPropertyGridChanged(wxPropertyGridEvent& event)
{
	if (m_currentSel != nullptr) {
		m_pg->Freeze();
		for (auto prop : m_propMap) m_currentSel->OnPropertyRefresh(m_pg, prop.first, prop.second);
		for (auto event : m_eventMap) m_currentSel->OnEventRefresh(m_pg, event.first, event.second);
		for (auto prop : m_propMap) {
			wxPGProperty* property = prop.first;
			if (property != nullptr) {
				wxPGProperty* parentProperty = property->GetParent();
				if (parentProperty->IsCategory() &&
					parentProperty->IsVisible() != (!property->HasFlag(wxPGFlags::Hidden))) {
					bool visible = false;
					for (unsigned int idx = 0; idx < parentProperty->GetChildCount(); idx++) {
						wxPGProperty* currChild = parentProperty->Item(idx);
						wxASSERT(currChild);
						if (!currChild->HasFlag(wxPGFlags::Hidden)) visible = true;
					}
					if (parentProperty->IsVisible() != visible) parentProperty->Hide(!visible);
				}
			}
			ibProperty* prop_ptr = prop.second;
			wxASSERT(prop_ptr);
			prop_ptr->RefreshPGProperty(property);
		}

		for (auto evt : m_eventMap) {
			ibEvent* event_ptr = evt.second;
			wxASSERT(event_ptr);
			event_ptr->RefreshPGProperty(evt.first);
		};

		m_pg->Thaw();
	}
	event.Skip();
}

void ibObjectInspector::OnPropertyGridExpand(wxPropertyGridEvent& event)
{
	m_isExpanded[event.GetPropertyName()] = event.GetProperty()->IsExpanded();

	wxPGProperty* egProp = m_pg->GetProperty(event.GetProperty()->GetName());
	if (egProp != nullptr) {
		if (event.GetProperty()->IsExpanded()) {
			m_pg->Expand(egProp);
		}
		else {
			m_pg->Collapse(egProp);
		}
	}
}

void ibObjectInspector::OnPropertyGridItemSelected(wxPropertyGridEvent& event)
{
	wxPGProperty* propPtr = event.GetProperty();
	if (propPtr != nullptr) {
		m_strSelPropItem = m_pg->GetPropertyName(propPtr);
		std::map< wxPGProperty*, ibProperty*>::iterator it = m_propMap.find(propPtr);
		if (m_propMap.end() == it) {
			// Could be a child property
			propPtr = propPtr->GetParent();
			it = m_propMap.find(propPtr);
		}
		if (m_currentSel && it != m_propMap.end()) {
			m_currentSel->OnPropertySelected(it->second);
		}
	}
}

void ibObjectInspector::OnPropertyGridRightClick(wxPropertyGridEvent& event)
{
	// Workmate-parity hook: surface "Сгенерировать через AI" when the
	// user right-clicks a synonym/comment/tooltip/title cell. Default
	// wxPropertyGrid behaviour (description in the help box) is
	// preserved for non-AI properties by calling event.Skip().
	wxPGProperty* propPtr = event.GetProperty();
	if (propPtr == nullptr || m_currentSel == nullptr) {
		event.Skip();
		return;
	}

	auto it = m_propMap.find(propPtr);
	if (it == m_propMap.end()) {
		// Composite/child properties — walk one level up like the
		// selection handler does.
		propPtr = propPtr->GetParent();
		it = m_propMap.find(propPtr);
	}

	if (it == m_propMap.end()) { event.Skip(); return; }

	ibProperty* prop = it->second;
	if (prop == nullptr || !ibAIPropertyHelper::IsAIEligible(prop->GetName())) {
		event.Skip();
		return;
	}

	// Mark the property as the active one so subsequent edits in the
	// helper land on the right cell, then pop the menu.
	m_pg->SelectProperty(propPtr, false);
	ibAIPropertyHelper::ShowContextMenu(m_pg->GetGrid(), m_pg,
	                                     propPtr, prop, m_currentSel);
}

///////////////////////////////////////////////////////////////////////////////

void ibObjectInspector::OnBitmapPropertyChanged(wxCommandEvent& event)
{
	wxLogDebug(wxT("OI::BitmapPropertyChanged: %s"), event.GetString().c_str());

	const wxString strPropName = event.GetString().BeforeFirst(':');
	wxString strPropVal = event.GetString().AfterFirst(':');

	//if (!propVal.IsEmpty()) {
	//	wxPGBitmapProperty* bp = wxDynamicCast(m_pg->GetPropertyByLabel(strPropName), wxPGBitmapProperty);
	//	if (bp != nullptr) {
	//		bp->UpdateChildValues(propVal);
	//	}
	//}
}

void ibObjectInspector::OnChildFocus(wxChildFocusEvent&) {
	// do nothing to avoid "scrollbar jump" if wx2.9 is used
}