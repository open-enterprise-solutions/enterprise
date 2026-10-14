#ifndef __OBJECT_INSPECT_H__
#define __OBJECT_INSPECT_H__

#include "frmclient/backend/propertyManager/propertyManager.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — the rows shown

#include <functional>
#include <memory>

#include <wx/aui/auibook.h>
#include <wx/propgrid/manager.h>
#include <wx/config.h>

#if !wxUSE_PROPGRID
#error "wxUSE_PROPGRID must be set to 1 in your wxWidgets library."
#endif

#include "frmclient/frmclient.h"

static int wxEVT_OES_PROP_PICTURE_CHANGED = wxNewEventType();

enum {
	wxOES_OI_DEFAULT_STYLE,
	wxOES_OI_MULTIPAGE_STYLE,
	wxOES_OI_SINGLE_PAGE_STYLE
};

#define objectInspector  ibObjectInspector::GetObjectInspector()

// ⭐ THE INSPECTOR SHOWS ROWS, never a property of its own. A row is a property as the server writes it (frmserver
// ibPropertyInspector: Name, Title, Help, Class, Value, Presentation, its choices, ReadOnly, Hidden; a category a row
// with no Class, its rows within), and its widget is the one registered for its class (ibPropertyRegistry). The rows
// come from the designer's frame — the object a person selected on the server — or are written here from an object
// the client holds itself (a spreadsheet's cell), the same way; an edit goes back as the row carries its value.
class FRMCLIENT_API ibObjectInspector final : public wxPanel {

	ibObjectInspector(wxWindow* parent, int id, int style = wxOES_OI_DEFAULT_STYLE);

public:

	// WHERE AN EDIT OF A ROW GOES — its property's name and the value as the row carries it (ibPropertyRegistry::
	// GetNodeValue). False: refused — the grid keeps the value it had.
	using ibPropertyEdit = std::function<bool (const wxString& name, const ibDataValue& value)>;

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

	// The object's push lands here (see ibPropertyObjectNotifier): resolve the ibProperty
	// to the row currently rendering it, by its name, and hide/reveal that one.
	bool PropertyHidden(const ibProperty* property, bool hide);

	// Services for the observers
	void SelectObject(ibPropertyObject* selobj, bool force = false) {

		if (IsShownInspector() && (force || m_currentSel != selobj)) {
			Create(selobj, force);
		}

		if (selobj != nullptr)
			ibJournalInfo(wxT("ui"), wxT("! <debug> activate property %s"), selobj->GetClassName());
		else
			ibJournalInfo(wxT("ui"), wxT("! <debug> clear property"));
	}

	// …ROWS DRAWN ELSEWHERE — the designer's frame (Properties: the server's ibPropertyInspector) — an edit going where
	// `edit` sends it. Sent again after every edit, as the object answered it: the grid is made anew, the row selected
	// staying selected. ⚠ Hidden too: the server sends an object's rows once (an empty patch while it is the one shown),
	// and the pane shown later showed nothing — the configuration's at the start, Properties asked of a row.
	void SelectObject(const ibDataNode& rows, ibPropertyEdit edit) {
		Create(rows, std::move(edit));
	}

	// The object shown — ALIVE, or none. A caller comparing it with another pointer must not be handed a
	// corpse's address: the next object built may be given the same one.
	ibPropertyObject* GetSelectedObject() const { return ShownObject(); }

	// ⭐ THE ONE QUESTION EVERY HANDLER ASKS BEFORE TOUCHING THE OBJECT: is the one shown still alive?
	// `m_currentSel` alone is an ADDRESS; the notifier's owner is the liveness flag (the object's dtor
	// clears it), so the two together answer. Measured 2026-09-15 (enterprise dump, 21:09): the object
	// shown died, the grid kept its rows, a layout update re-selected one — and the selection handler
	// called into the freed object (its vtable read as 0xDDDDDDDD). Create asked this; the three grid
	// handlers did not.
	ibPropertyObject* ShownObject() const {
		return m_currentSel != nullptr && m_notifier->GetOwner() == m_currentSel ? m_currentSel : nullptr;
	}

	bool IsShownInspector() const;
	void ShowInspector();

	void SavePosition();

	static ibObjectInspector* GetObjectInspector();

protected:

	void OnPropertyGridChanging(wxPropertyGridEvent& event);
	void OnPropertyGridChanged(wxPropertyGridEvent& event);
	void OnPropertyGridExpand(wxPropertyGridEvent& event);
	void OnPropertyGridItemSelected(wxPropertyGridEvent& event);

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

	friend class ibFrontendMainFrame;

	wxPropertyGridManager* CreatePropertyGridManager(wxWindow* parent, wxWindowID id) const;

	// THE OBJECT THE CLIENT HOLDS — its rows written here (ibPropertyRegistry::AppendProperty), an edit put through its
	// door (ModifyProperty).
	void Create(ibPropertyObject* object, bool force = false);
	// …THE ROWS SENT — none of the client's objects shown.
	void Create(const ibDataNode& rows, ibPropertyEdit edit);

	// A rebuild asked while a grid event is in flight, or one already queued — the last one asked, after the event.
	void ScheduleRebuild();

	// The grid of the rows shown (m_rows): a category's rows within it, each by its class's widget.
	void CreateRows();
	void AddItems(const ibDataNode& category, wxPGProperty* parent);
	// …and a category with nothing left to show hidden with its rows.
	void RefreshCategories();

	// The client's own object's property or event of that name — or its sizer item's, as a form's control shows both.
	ibProperty* GetProperty(const wxString& name) const;
	ibEvent* GetEvent(const wxString& name) const;
	ibPropertyObject* GetSizerItem() const;

	// The edit of the object the client holds, through its door (ibPropertyGate) — asked of the object shown.
	bool ModifyProperty(const wxString& name, const ibDataValue& value);

	void RestoreLastSelectedPropItem();

	wxPropertyGridManager* m_pg;

	std::map< wxString, bool > m_isExpanded;

	// The rows shown, the widget of each, and where an edit of one goes.
	ibDataNode m_rows;
	std::map<wxPGProperty*, const ibDataNode*> m_rowMap;
	ibPropertyEdit m_edit;

	ibPropertyObject* m_currentSel;

	// Our end of m_currentSel's push channel, registered on it for exactly as long as we show
	// it. Its owner doubles as the liveness flag: null means m_currentSel died under us.
	std::unique_ptr<ibPropertyObjectNotifier> m_notifier;

	// Deferred / coalesced rebuild. A child edit bubbles back here to re-Create the grid, but Create()
	// Clear()s it — fatal if a wxPG change event is still dispatching on one of those properties, and
	// wasteful when several selects fire in one refresh burst. m_inGridEvent marks "an event is in flight"
	// (set by the change handlers); m_rebuildScheduled marks "a rebuild is already queued". While either
	// holds, Create() stashes the target and posts ONE CallAfter instead of rebuilding inline. The target
	// is an object or the rows sent — whichever was asked last.
	bool m_inGridEvent = false;
	bool m_rebuildScheduled = false;
	ibPropertyObject* m_pendingObject = nullptr;
	bool m_pendingForce = false;
	std::unique_ptr<ibDataNode> m_pendingRows;
	ibPropertyEdit m_pendingEdit;

	int m_style;

	//save the current selected property
	wxString m_strSelPropItem;

	wxDECLARE_EVENT_TABLE();
};

#endif //__OBJ_INSPECT__
