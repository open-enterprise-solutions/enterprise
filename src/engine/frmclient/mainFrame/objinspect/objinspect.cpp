#include "objinspect.h"
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

#include <set>

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

	// ONE CATEGORY OF AN OBJECT THE CLIENT HOLDS, as rows — walked as the server walks one (frmserver
	// ibPropertyInspector): its properties, its events, then its categories, each a row with its Title and its own; a
	// property met before is not met again. The object fills a property's value first (OnPropertyCreated — a
	// spreadsheet's cell reads its selection), so the row carries what the object holds now.
	void AppendCategory(ibDataNode& parent, ibPropertyObject* object, const ibPropertyCategory* category,
		std::set<wxString>& met)
	{
		if (category == nullptr)
			return;
		for (unsigned int index = 0; index < category->GetPropertyCount(); ++index) {
			ibProperty* const property = object->GetProperty(category->GetPropertyName(index));
			if (property == nullptr || !met.insert(property->GetName()).second)
				continue;
			object->OnPropertyCreated(property);
			ibPropertyRegistry::AppendProperty(parent, property);
		}
		for (unsigned int index = 0; index < category->GetEventCount(); ++index) {
			ibEvent* const event = object->GetEvent(category->GetEventName(index));
			if (event == nullptr || !met.insert(event->GetName()).second)
				continue;
			ibPropertyRegistry::AppendProperty(parent, event);
		}
		for (unsigned int index = 0; index < category->GetCategoryCount(); ++index) {
			const ibPropertyCategory* const nextCategory = category->GetCategory(index);
			if (nextCategory == nullptr)
				continue;
			ibDataNode& row = parent.AddChild(0, static_cast<ibMetaID>(parent.Children().size() + 1));
			row.SetValue(wxT("Name"), nextCategory->GetName());
			row.SetValue(wxT("Title"), nextCategory->GetLabel());
			AppendCategory(row, object, nextCategory, met);
		}
	}
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

#include "frmclient/mainFrame/mainFrame.h"

ibObjectInspector* ibObjectInspector::GetObjectInspector()
{
	return ibFrontendMainFrame::GetObjectInspector();
}

#include "frmclient/visualView/formdefs.h"

void ibObjectInspector::Create(ibPropertyObject* object, bool force)
{
	// The DEFERRED rebuild (the CallAfter below) reads m_pendingObject at FIRE time, so it must ALWAYS track the
	// latest requested object — record it on EVERY call, not only when we defer. Otherwise a rebuild queued for object
	// A that is then re-selected to B (e.g. a form closing hands the inspector to its metaobject) would still fire on
	// the now-STALE A: if A was freed in the meantime the deferred Create dereferences a corpse (the floating crash).
	// With this, the queued rebuild always sees the REAL current target (B), so the reassignment "wins".
	m_pendingObject = object;
	m_pendingRows.reset();

	// Defer + coalesce. A child edit routes back here to rebuild (RefreshEditor → attribute tree →
	// SelectObject → Create), but the m_pg->Clear() below DESTROYS every wxPGProperty. If a wxPG change
	// event is still being dispatched, that frees the very property wxPG is editing → use-after-free the
	// moment the handler returns; and a burst of selects in one refresh would each Clear+refill (flicker,
	// the "re-population on every child" churn). So while an event is in flight or a rebuild is already
	// queued, record the target and post a SINGLE CallAfter — the grid rebuilds once, after the stack
	// unwinds, on the last requested object. A plain selection (no event, nothing queued) rebuilds inline.
	if (m_inGridEvent || m_rebuildScheduled) {
		m_pendingForce = m_pendingForce || force;
		ScheduleRebuild();
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

		// Its rows — the object's own categories, then its sizer item's (a form's control shows where it sits too).
		m_rows = ibDataNode();
		m_edit = nullptr;
		if (object != nullptr) {
			std::set<wxString> met;
			AppendCategory(m_rows, object, object->GetCategory(), met);
			if (ibPropertyObject* const sizerItem = GetSizerItem())
				AppendCategory(m_rows, sizerItem, sizerItem->GetCategory(), met);
			m_edit = [this](const wxString& name, const ibDataValue& value) { return ModifyProperty(name, value); };
		}

		CreateRows();

		if (object != nullptr)
			m_currentSel->OnPropertyCreated();
	}

	// ShownObject, not m_currentSel: asked again with the address of an object that has since died,
	// the branch above does not rebuild — and the pointer left standing is a corpse.
	if (ShownObject() != nullptr) {
		m_currentSel->OnRefresh();
		RefreshCategories();
	}

	RestoreLastSelectedPropItem();
}

void ibObjectInspector::Create(const ibDataNode& rows, ibPropertyEdit edit)
{
	// Asked while an event is in flight — after it, as an object is (see above): the rows an edit brings back arrive
	// inside that edit's own handler.
	if (m_inGridEvent || m_rebuildScheduled) {
		m_pendingRows = std::make_unique<ibDataNode>(rows);
		m_pendingEdit = std::move(edit);
		ScheduleRebuild();
		return;
	}

	wxWindowUpdateLocker updateLock(m_pg);

	// None of the client's objects is shown: the one that was leaves, as above.
	if (m_currentSel != nullptr && m_notifier->GetOwner() == m_currentSel)
		m_currentSel->RemoveNotifier(m_notifier.get());
	m_currentSel = nullptr;
	m_pendingObject = nullptr;

	m_rows = rows;
	m_edit = std::move(edit);
	CreateRows();
	RefreshCategories();

	RestoreLastSelectedPropItem();
}

void ibObjectInspector::ScheduleRebuild()
{
	if (m_rebuildScheduled)
		return;
	m_rebuildScheduled = true;
	CallAfter([this] {
		m_rebuildScheduled = false;
		if (m_pendingRows != nullptr) {
			const std::unique_ptr<ibDataNode> rows = std::move(m_pendingRows);
			Create(*rows, std::move(m_pendingEdit));
			return;
		}
		const bool pendingForce = m_pendingForce;
		m_pendingForce = false;
		Create(m_pendingObject, pendingForce);
	});
}

void ibObjectInspector::CreateRows()
{
	m_pg->Clear();
	m_rowMap.clear();

	if (!m_rows.Children().empty()) {
		m_pg->AddPage(wxT("default"));
		m_pg->SelectPage(0);
		AddItems(m_rows, nullptr);
		m_pg->SetPropertyAttributeAll(wxPG_BOOL_USE_CHECKBOX, (long)1);
	}

	m_pg->Refresh();
	m_pg->Update();
}

void ibObjectInspector::AddItems(const ibDataNode& category, wxPGProperty* parent)
{
	const wxString categoryName = ibPropertyRegistry::GetName(category);
	for (const ibDataNode& row : category.Children()) {

		wxPGProperty* const property = ibPropertyRegistry::Create(row);

		// A CATEGORY — a row with no class; one with nothing in it is not drawn.
		if (property == nullptr) {
			if (row.Children().empty())
				continue;
			wxPropertyCategory* const nextCategory =
				new wxPropertyCategory(ibPropertyRegistry::GetLabel(row), ibPropertyRegistry::GetName(row));
			wxPGProperty* const catId = parent != nullptr ? m_pg->AppendIn(parent, nextCategory) : m_pg->Append(nextCategory);
			AddItems(row, catId);
			std::map< wxString, bool >::iterator it = m_isExpanded.find(ibPropertyRegistry::GetName(row));
			if (it != m_isExpanded.end()) {
				if (it->second) {
					m_pg->Expand(catId);
				}
				else {
					m_pg->Collapse(catId);
				}
			}
			continue;
		}

		wxPGProperty* const id = parent != nullptr ? m_pg->AppendIn(parent, property) : m_pg->Append(property);
		if (m_style != wxOES_OI_MULTIPAGE_STYLE) {
			// Per-class tint — interior palette. Window props get
			// the warm focal hue (light terracotta), common get
			// neutral cream, sizerItem gets the cool tier (light
			// powder). Replaces XP-era yellow/light-blue/cyan.
			if (stringUtils::CompareString(categoryName, wxT("window")))
				m_pg->SetPropertyBackgroundColour(id, wxColour(0xF8, 0xE3, 0xD5)); // light terracotta
			else if (stringUtils::CompareString(categoryName, wxT("common")))
				m_pg->SetPropertyBackgroundColour(id, wxColour(0xF5, 0xE8, 0xD5)); // light cream
			else if (stringUtils::CompareString(categoryName, wxT("sizerItem")))
				m_pg->SetPropertyBackgroundColour(id, wxColour(0xE6, 0xEE, 0xF5)); // light powder
		}

		std::map< wxString, bool >::iterator it = m_isExpanded.find(ibPropertyRegistry::GetName(row));
		if (it != m_isExpanded.end()) {
			if (it->second) {
				m_pg->Expand(id);
			}
			else {
				m_pg->Collapse(id);
			}
		}

		id->RefreshChildren();
		m_rowMap.emplace(id, &row);
	}
}

void ibObjectInspector::RefreshCategories()
{
	for (const auto& shown : m_rowMap) {
		wxPGProperty* property = shown.first;
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

ibPropertyObject* ibObjectInspector::GetSizerItem() const
{
	ibPropertyObject* const object = ShownObject();
	ibPropertyObject* const owner = object != nullptr ? object->GetOwner() : nullptr;
	return owner != nullptr && owner->GetComponentType() == COMPONENT_TYPE_SIZERITEM ? owner : nullptr;
}

ibProperty* ibObjectInspector::GetProperty(const wxString& name) const
{
	ibPropertyObject* const object = ShownObject();
	if (object == nullptr)
		return nullptr;
	if (ibProperty* const property = object->GetProperty(name))
		return property;
	ibPropertyObject* const sizerItem = GetSizerItem();
	return sizerItem != nullptr ? sizerItem->GetProperty(name) : nullptr;
}

ibEvent* ibObjectInspector::GetEvent(const wxString& name) const
{
	ibPropertyObject* const object = ShownObject();
	if (object == nullptr)
		return nullptr;
	if (ibEvent* const event = object->GetEvent(name))
		return event;
	ibPropertyObject* const sizerItem = GetSizerItem();
	return sizerItem != nullptr ? sizerItem->GetEvent(name) : nullptr;
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

#include "frmclient/visualView/ctrl/frame.h"

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
	// m_rowMap is keyed the way rendering needs it (wxPGProperty → row); the push arrives the
	// other way round, so walk it, by the property's name. A property set is a screenful, and an
	// object pushes only for the few properties it owns.
	for (const auto& shown : m_rowMap) {
		if (ibPropertyRegistry::GetName(*shown.second) == property->GetName())
			return HideProperty(shown.first, hide);   // recursive, as the wxPG default: these are
	}                                                 // the object's OWN leaf properties, and a
	return false;                                     // composite's children stay its editor's business
}

// ⭐ THROUGH THE BACKEND'S DOOR — ibPropertyGate::SetValue, which is the four steps that used to be
// written out here. They were the authority, and being written HERE meant a caller with no
// inspector (the MCP server) had to carry its own copy of them. Same sequence, one home, and the
// selection is what this window contributes: the property being edited may belong to a nested child
// the selection only accumulates, so `asked` and the property's owner are not always the same.
bool ibObjectInspector::ModifyProperty(const wxString& name, const ibDataValue& value)
{
	if (ibProperty* const property = GetProperty(name))
		return ibPropertyGate::SetValue(m_currentSel, property, value);
	if (ibEvent* const event = GetEvent(name))
		return ibPropertyGate::SetEvent(m_currentSel, event, value);
	return false;
}

void ibObjectInspector::RestoreLastSelectedPropItem()
{
	// The row selected before — the rows are sent again after every edit — or else the name.
	wxPGProperty* propPtr = !m_strSelPropItem.IsEmpty() ? m_pg->GetPropertyByName(m_strSelPropItem) : nullptr;
	if (propPtr == nullptr)
		propPtr = m_pg->GetPropertyByName(wxT("name"));
	if (propPtr != nullptr) {

		m_pg->SelectProperty(propPtr, false);

		wxPGProperty* rowPtr = propPtr;
		std::map<wxPGProperty*, const ibDataNode*>::iterator it = m_rowMap.find(rowPtr);
		if (it == m_rowMap.end() && rowPtr->GetParent() != nullptr)
			it = m_rowMap.find(rowPtr->GetParent());   // a child of a composite: its row's help
		if (it != m_rowMap.end()) {
			// Update displayed description for the new selection
			const wxString& localized = wxGetTranslation(it->second->GetValue<wxString>(wxT("Help")));
			m_pg->SetPropertyHelpString(propPtr, localized);
			m_pg->SetDescription(propPtr->GetLabel(), localized);
			return;
		}
	}

	m_pg->SetDescription(wxEmptyString, wxEmptyString);
}

///////////////////////////////////////////////////////////////////////////////

void ibObjectInspector::OnPropertyGridChanging(wxPropertyGridEvent& event)
{
	ScopedFlag inEvent(m_inGridEvent);   // an edit here may bubble back to Create — keep it deferred

	// A DEAD OBJECT'S ROWS take no edit — and nothing of it may be read: its ibProperties died
	// with it. The grid is cleared once the event unwinds (Create defers itself while one is in flight).
	// Rows sent from elsewhere have no object here, and stay.
	if (m_currentSel != nullptr && ShownObject() == nullptr) {
		event.Veto();
		Create(nullptr, true);
		return;
	}

	wxPGProperty* propPtr = event.GetProperty();
	std::map<wxPGProperty*, const ibDataNode*>::iterator it = m_rowMap.find(propPtr);
	if (it != m_rowMap.end()) {
		const ibDataNode& row = *it->second;
		// …held while the edit runs: the rows it brings back wait for the event (Create defers itself).
		const ibPropertyEdit edit = m_edit;
		if (!edit || !edit(ibPropertyRegistry::GetName(row), ibPropertyRegistry::GetNodeValue(row, event.GetPropertyValue())))
			event.Veto();
		// Update displayed description for the new selection
		const wxString& localized = wxGetTranslation(row.GetValue<wxString>(wxT("Help")));
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
	// in the object shown. Asking them here would call into a freed ibProperty (use-after-free — the
	// crash). Skip; the deferred Create writes the rows again from the live property set.
	if (m_rebuildScheduled) {
		event.Skip();
		return;
	}

	// Rows sent from elsewhere: what the edit changed comes back as rows (Create, after this event).
	if (m_currentSel == nullptr) {
		event.Skip();
		return;
	}

	// The object shown died while the edit was in flight: nothing of it may be refreshed, and the grid
	// goes — after this event (see OnPropertyGridChanging).
	if (ShownObject() == nullptr) {
		Create(nullptr, true);
		event.Skip();
		return;
	}

	// ⭐ A LIST WHOSE CHOICES FOLLOW ANOTHER PROPERTY — an event handler's Event lists what its Source raises. A
	// row takes its choices when it is made, so after an edit every one-of-a-list property is asked for its list
	// again (GetValueList, the property's own answer), and where it no longer matches the row the grid is made
	// anew — after this event, as a schedule's links are (ibPGCalcScheduleProperty::RefreshChildren).
	//
	// 🛑 ASKED OF THE PROPERTY, NOT OF THE ROW: a list made EMPTY — a new handler, no source yet — has no
	// choices at all (wxPGChoices::IsOk is false), and a check that started from the row skipped exactly it.
	for (const auto& row : m_rowMap) {
		ibProperty* const property = row.first != nullptr ? GetProperty(ibPropertyRegistry::GetName(*row.second)) : nullptr;
		if (property == nullptr)
			continue;
		ibPropertyChoiceList asked;
		if (property->GetValueList(asked) != ibPropertyChoiceMode::Single)
			continue;   // not one of a list: nothing to go stale
		const wxPGChoices& shown = row.first->GetChoices();
		bool same = asked.GetCount() == shown.GetCount();
		for (unsigned int idx = 0; same && idx < asked.GetCount(); idx++)
			same = asked.GetId(idx) == shown.GetValue(idx);
		if (!same) {
			Create(m_currentSel, true);
			event.Skip();
			return;
		}
	}

	wxWindowUpdateLocker updateLock(m_pg);   // RAII Thaw — a throwing OnPropertyRefresh must not leave the grid frozen

	m_currentSel->OnRefresh();
	RefreshCategories();
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
	// ⚠ A SELECTION ARRIVES WITHOUT ANYBODY CLICKING: the grid re-selects its row when it is thawed,
	// and a layout update thaws it (ibFrontendMainFrame::UpdateFrameManager → wxAuiManager::Update).
	// That is how a dead object's row was selected in the dump of 2026-09-15. So the question is asked
	// here too, and a rebuild asked from here waits for wxPG to finish selecting.
	ScopedFlag inEvent(m_inGridEvent);

	if (m_currentSel != nullptr && ShownObject() == nullptr) {
		Create(nullptr, true);
		return;
	}

	wxPGProperty* propPtr = event.GetProperty();
	if (propPtr != nullptr) {
		m_strSelPropItem = m_pg->GetPropertyName(propPtr);
		std::map<wxPGProperty*, const ibDataNode*>::iterator it = m_rowMap.find(propPtr);
		if (m_rowMap.end() == it) {
			// Could be a child property
			propPtr = propPtr->GetParent();
			it = m_rowMap.find(propPtr);
		}
		// …the client's own object is told which of its properties a person is at.
		if (it != m_rowMap.end() && ShownObject() != nullptr) {
			if (ibProperty* const property = GetProperty(ibPropertyRegistry::GetName(*it->second)))
				m_currentSel->OnPropertySelected(property);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

void ibObjectInspector::OnBitmapPropertyChanged(wxCommandEvent& event)
{
	ibJournalInfo(wxT("ui"), wxT("OI::BitmapPropertyChanged: %s"), event.GetString().c_str());

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