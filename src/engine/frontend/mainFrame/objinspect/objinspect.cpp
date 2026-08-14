#include "objinspect.h"
#include "frontend/propertyManager/property/private/propertyRegistry.h"

#include <wx/wupdlock.h>   // wxWindowUpdateLocker — RAII Freeze/Thaw (guaranteed Thaw on scope exit)

enum {
	WXOES_PROPERTY_GRID = wxID_HIGHEST + 1000
};

namespace {
	// RAII: mark a wxPG change event "in flight" for its whole handler, so any rebuild the edit triggers
	// (Create) defers instead of Clear()ing the grid under the property wxPG is still dispatching on. Restores
	// on scope exit even through the handler's early returns / Veto paths.
	struct ScopedFlag {
		bool& m_flag;
		explicit ScopedFlag(bool& flag) : m_flag(flag) { m_flag = true; }
		~ScopedFlag() { m_flag = false; }
	};
}

// ---------------------------------------------------------
// ibGenericPropertyObjectNotifier
// ---------------------------------------------------------

// The inspector's end of the object's push channel: turns "this ibProperty is now
// hidden" into the wxPG call. The object never names a widget; the mapping back to
// wxPGProperty lives here, where the grid does.
class ibGenericPropertyObjectNotifier : public ibPropertyObjectNotifier
{
public:

	ibGenericPropertyObjectNotifier(ibObjectInspector* inspector)
	{
		m_inspector = inspector;
	}

	virtual bool PropertyHidden(const ibProperty* property, bool hide) wxOVERRIDE
	{
		return m_inspector->PropertyHidden(property, hide);
	}

private:

	ibObjectInspector* m_inspector;
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
EVT_CHILD_FOCUS(ibObjectInspector::OnChildFocus)
wxEND_EVENT_TABLE()

///////////////////////////////////////////////////////////////////////////////
// ibObjectInspector
///////////////////////////////////////////////////////////////////////////////

ibObjectInspector::ibObjectInspector(wxWindow* parent, int id, int style)
	: wxPanel(parent, id), m_currentSel(nullptr)
	, m_notifier(new ibGenericPropertyObjectNotifier(this))
	, m_style(style)
{
	m_pg = CreatePropertyGridManager(this, WXOES_PROPERTY_GRID);

	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
	topSizer->Add(m_pg, 1, wxALL | wxEXPAND, 0);
	SetSizer(topSizer);

	ibObjectInspector::Connect(wxID_ANY, wxEVT_OES_PROP_PICTURE_CHANGED, wxCommandEventHandler(ibObjectInspector::OnBitmapPropertyChanged));
}

ibObjectInspector::~ibObjectInspector()
{
	// The other direction: we die first, and the object we were showing outlives us holding a
	// pointer to our notifier. Leave its list before that pointer goes stale.
	if (m_currentSel != nullptr && m_notifier->GetOwner() == m_currentSel)
		m_currentSel->RemoveNotifier(m_notifier.get());

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
	return ibFrontendMainFrame::GetObjectInspector();
}

#include "frontend/visualView/formdefs.h"

void ibObjectInspector::Create(ibPropertyObject* object, bool force)
{
	// The DEFERRED rebuild (the CallAfter below) reads m_pendingObject at FIRE time, so it must ALWAYS track the
	// latest requested object — record it on EVERY call, not only when we defer. Otherwise a rebuild queued for object
	// A that is then re-selected to B (e.g. a form closing hands the inspector to its metaobject) would still fire on
	// the now-STALE A: if A was freed in the meantime the deferred Create dereferences a corpse (the floating crash).
	// With this, the queued rebuild always sees the REAL current target (B), so the reassignment "wins".
	m_pendingObject = object;

	// Defer + coalesce. A child edit routes back here to rebuild (RefreshEditor → attribute tree →
	// SelectObject → Create), but the m_pg->Clear() below DESTROYS every wxPGProperty. If a wxPG change
	// event is still being dispatched, that frees the very property wxPG is editing → use-after-free the
	// moment the handler returns; and a burst of selects in one refresh would each Clear+refill (flicker,
	// the "re-population on every child" churn). So while an event is in flight or a rebuild is already
	// queued, record the target and post a SINGLE CallAfter — the grid rebuilds once, after the stack
	// unwinds, on the last requested object. A plain selection (no event, nothing queued) rebuilds inline.
	if (m_inGridEvent || m_rebuildScheduled) {
		m_pendingForce = m_pendingForce || force;
		if (!m_rebuildScheduled) {
			m_rebuildScheduled = true;
			CallAfter([this] {
				m_rebuildScheduled = false;
				const bool pendingForce = m_pendingForce;
				m_pendingForce = false;
				Create(m_pendingObject, pendingForce);
			});
		}
		return;
	}

	// RAII Freeze/Thaw: Thaw MUST run even if a rebuild step throws (e.g. GetClassName on an
	// unexpected object) or returns early. A skipped Thaw leaves the grid frozen forever (the
	// "freeze" bug) AND unbalances the freeze count so later rebuilds stop suppressing paint
	// (the flicker bug). wxWindowUpdateLocker Thaws in its dtor at block exit.
	wxWindowUpdateLocker updateLock(m_pg);

	if (force || object != m_currentSel) {

		// The object pushes its presentation through the notifier, so it must be registered on
		// exactly the object we are showing — leave the old one first. A null owner means that
		// object already died under us (its dtor cleared it): m_currentSel is a corpse and must
		// NOT be dereferenced to unregister — it already dropped every notifier it had.
		if (m_currentSel != nullptr && m_notifier->GetOwner() == m_currentSel)
			m_currentSel->RemoveNotifier(m_notifier.get());

		m_currentSel = object;

		if (m_currentSel != nullptr)
			m_currentSel->AddNotifier(m_notifier.get());

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
	}

	if (m_currentSel != nullptr) {
		m_currentSel->OnRefresh();
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

	// Margin + category captions = light dusty blue (one tier between
	// cream cells and powder-blue chrome). Replaces darkened cream that
	// looked muddy against the new palette.
	pg->GetGrid()->SetMarginColour(wxColour(0xE6, 0xEE, 0xF5));            // #E6EEF5 light powder

	pg->GetGrid()->SetCaptionBackgroundColour(wxColour(0xC8, 0xD6, 0xDF)); // #C8D6DF light dusty

	pg->GetGrid()->SetCaptionTextColour(wxColour(0x3F, 0x5C, 0x77));       // #3F5C77 deep dusty blue
	pg->GetGrid()->SetCellDisabledTextColour(wxColour(0x94, 0xA6, 0xB4));  // #94A6B4 dusty blue-grey

	pg->GetGrid()->SetCellTextColour(*wxBLACK);

	return pg;
}

bool ibObjectInspector::PropertyHidden(const ibProperty* property, bool hide)
{
	// m_propMap is keyed the way rendering needs it (wxPGProperty → ibProperty); the push
	// arrives the other way round, so walk it. A property set is a screenful, and an object
	// pushes only for the few properties it owns — a second map would cost more to keep in
	// step (Clear() rebuilds it wholesale) than this walk costs to run.
	for (const auto& prop : m_propMap) {
		if (prop.second == property)
			return HideProperty(prop.first, hide);   // recursive, as the wxPG default: these are
	}                                                // the object's OWN leaf properties, and a
	return false;                                    // composite's children stay its editor's business
}

wxPGProperty* ibObjectInspector::GetProperty(ibProperty* prop) const
{
	wxPGProperty* result = ibPropertyRegistry::Create(prop);
	if (result != nullptr) {
		result->SetHelpString(prop->GetHelp());
		result->Enable(prop->IsEditable());
	}
	return result;
}

wxPGProperty* ibObjectInspector::GetEvent(ibEvent* event) const
{
	wxPGProperty* result = ibPropertyRegistry::Create(event);
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
		// Raise the change from the property's REAL owner — which may be a NESTED child the inspected holder only
		// ACCUMULATES (a dynamic list under a form-attribute holder: m_currentSel is the holder, but the edited
		// Source belongs to the list). Call the OWNER's own OnChildChanged so the CHILD initialises its reaction
		// and then bubbles up the attach chain to the holder. A self-refreshing owner (a control) has no attach
		// owner, so the bubble stops and this is a no-op for it.
		if (ibPropertyObject* owner = prop->GetPropertyObject())
			owner->OnChildChanged();
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
		// Same as a property edit: raise it from the event's REAL owner so a nested child initialises + bubbles
		// up the attach chain to a frontend holder that re-renders.
		if (ibPropertyObject* owner = event->GetPropertyObject())
			owner->OnChildChanged();
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////

void ibObjectInspector::OnPropertyGridChanging(wxPropertyGridEvent& event)
{
	ScopedFlag inEvent(m_inGridEvent);   // an edit here may bubble back to Create — keep it deferred
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
	ScopedFlag inEvent(m_inGridEvent);       // same as Changing — defer any rebuild the refresh triggers

	// The edit already invalidated the property set and a rebuild is QUEUED (deferred out of the event): an
	// attribute Type change re-materialises its held value, freeing the old value's ibProperties that are still
	// in m_propMap. Walking the stale map here would call RefreshPGProperty on a freed ibProperty (use-after-
	// free — the crash). Skip; the deferred Create rebuilds m_propMap with the live property set.
	if (m_rebuildScheduled) {
		event.Skip();
		return;
	}

	wxWindowUpdateLocker updateLock(m_pg);   // RAII Thaw — a throwing OnPropertyRefresh must not leave the grid frozen

	if (m_currentSel != nullptr) {
		m_currentSel->OnRefresh();
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
		}
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

///////////////////////////////////////////////////////////////////////////////

void ibObjectInspector::OnBitmapPropertyChanged(wxCommandEvent& event)
{
	wxLogDebug(wxT("OI::BitmapPropertyChanged: %s"), event.GetString().c_str());

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