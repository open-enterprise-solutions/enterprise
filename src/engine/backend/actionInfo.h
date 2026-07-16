#ifndef __ACTION_INFO_H__
#define __ACTION_INFO_H__

struct ibActionDescription {

	ibActionID m_lAction;
	wxString m_strAction;

	ibActionDescription(const ibActionID& lAction) : m_lAction(lAction), m_strAction() {}
	ibActionDescription(const wxString& strAction) : m_lAction(wxNOT_FOUND), m_strAction(strAction) {}

	wxString GetCustomAction() const { return m_strAction; }
	ibActionID GetSystemAction() const { return m_lAction; }

	bool operator == (const ibActionDescription& rhs) const {
		if (m_lAction == wxNOT_FOUND)
			return m_strAction == rhs.m_strAction;
		return m_lAction == rhs.m_lAction;
	}

	friend class ibActionDescriptionMemory;
};

class ibActionDescriptionMemory {
public:
	// node form
	static bool ReadNode(const class ibDataValue& value, ibActionDescription& actionDesc);
	static bool WriteNode(class ibDataValue& value, const ibActionDescription& actionDesc);
};

#include "backend_picture.h"

struct ibCommandItem {
	ibActionID           m_actionId = wxNOT_FOUND;   // the action's id (wxNOT_FOUND = a separator)
	wxString             m_name;
	wxString             m_caption;
	ibPictureDescription m_pictureDescription;
	bool                 m_pictureAndText = false;   // show picture AND caption (else picture-only when it has one)
	bool                 m_createInForm   = false;   // put this command on the form by default (vs available-only)
	ibValue*             m_srcData        = nullptr;
	// Does running this action MODIFY data? Default true — a view-only form greys every data-modifying command
	// (Save / Post / Mark-for-delete / Create / …); read-only actions (Refresh / Filter / Sort / open) set it
	// false (SetModify) so they stay live. Only the view-only greying reads it (BuildCommands).
	bool                 m_modifiesData   = true;

	ibCommandItem() {}
	ibCommandItem(const ibActionID& actionId, const wxString& name, const wxString& caption,
		const ibPictureDescription& pictureDescription, bool pictureAndText = false, bool createInForm = false, ibValue* srcData = nullptr)
		: m_actionId(actionId), m_name(name), m_caption(caption), m_pictureDescription(pictureDescription),
		  m_pictureAndText(pictureAndText), m_createInForm(createInForm), m_srcData(srcData) {}

	// Fluent setters — chain right after AddAction: `actions.AddAction(…, eGenerate).SetModify(false)`.
	ibCommandItem& SetModify(bool modifies)      { m_modifiesData   = modifies; return *this; }
	ibCommandItem& SetPictureAndText(bool value) { m_pictureAndText = value;    return *this; }
	ibCommandItem& SetCreateInForm(bool value)   { m_createInForm   = value;    return *this; }
};

class ibActionDataObject {
protected:

	class ibActionCollection {

		ibValue* m_srcData;
		std::vector<ibCommandItem> m_vecAction;

	private:

		// The single lookup primitive — every id-keyed accessor routes through here (nullptr on miss).
		const ibCommandItem* FindByID(const ibActionID& lNumAction) const {
			auto iterator = std::find_if(m_vecAction.begin(), m_vecAction.end(), [lNumAction](const ibCommandItem& act) {
				return lNumAction == act.m_actionId; });
			return iterator != m_vecAction.end() ? &*iterator : nullptr;
		}

		bool ExistsAction(const ibActionID& lNumAction) const { return FindByID(lNumAction) != nullptr; }

		// The single construction/placement primitive — index < 0 appends at the end, otherwise inserts at
		// index. All AddAction / InsertAction wrappers funnel through here (field order defined in ONE place),
		// and it returns the placed item BY REF so a caller can chain a fluent setter right after.
		ibCommandItem& EmplaceAction(int index, const ibActionID& lNumAction, const wxString& name, const wxString& caption,
			const ibPictureDescription& pictureDescription, bool pictureAndText, bool createInForm, ibValue* srcData) {
			wxASSERT(!ExistsAction(lNumAction));
			ibValue* src = srcData ? srcData : m_srcData;
			if (index < 0) {
				m_vecAction.emplace_back(lNumAction, name, caption, pictureDescription, pictureAndText, createInForm, src);
				return m_vecAction.back();
			}
			return *m_vecAction.insert(m_vecAction.begin() + index,
				ibCommandItem(lNumAction, name, caption, pictureDescription, pictureAndText, createInForm, src));
		}

	public:

		ibActionCollection(ibValue* srcData = nullptr) : m_srcData(srcData) {}

		// ---- append at the end (returns the item BY REF — chain .SetModify(...) etc right after) -------------

		ibCommandItem& AddAction(const wxString& name, const ibActionID& lNumAction, bool createInForm = true, ibValue* srcData = nullptr) {
			return EmplaceAction(-1, lNumAction, name, wxEmptyString, ibPictureDescription(), false, createInForm, srcData);
		}

		ibCommandItem& AddAction(const wxString& name, const wxString& caption, const ibActionID& lNumAction, bool createInForm = true, ibValue* srcData = nullptr) {
			return EmplaceAction(-1, lNumAction, name, caption, ibPictureDescription(), false, createInForm, srcData);
		}

		ibCommandItem& AddAction(const wxString& name, const wxString& caption, const ibPictureDescription& pictureDescription, const ibActionID& lNumAction, bool createInForm = true, ibValue* srcData = nullptr) {
			return EmplaceAction(-1, lNumAction, name, caption, pictureDescription, false, createInForm, srcData);
		}

		ibCommandItem& AddAction(const wxString& name, const wxString& caption, const ibPictureDescription& pictureDescription, bool pictureAndText, const ibActionID& lNumAction, bool createInForm = true, ibValue* srcData = nullptr) {
			return EmplaceAction(-1, lNumAction, name, caption, pictureDescription, pictureAndText, createInForm, srcData);
		}

		void AddSeparator() { m_vecAction.emplace_back(); }

		// ---- insert at index (also returns the item BY REF) -------------------

		ibCommandItem& InsertAction(unsigned int index, const wxString& name, const ibActionID& lNumAction, bool createInForm = true, ibValue* srcData = nullptr) {
			return EmplaceAction((int)index, lNumAction, name, wxEmptyString, ibPictureDescription(), false, createInForm, srcData);
		}

		ibCommandItem& InsertAction(unsigned int index, const wxString& name, const wxString& caption, const ibActionID& lNumAction, bool createInForm = true, ibValue* srcData = nullptr) {
			return EmplaceAction((int)index, lNumAction, name, caption, ibPictureDescription(), false, createInForm, srcData);
		}

		ibCommandItem& InsertAction(unsigned int index, const wxString& name, const wxString& caption, const ibPictureDescription& pictureDescription, const ibActionID& lNumAction, bool createInForm = true, ibValue* srcData = nullptr) {
			return EmplaceAction((int)index, lNumAction, name, caption, pictureDescription, false, createInForm, srcData);
		}

		ibCommandItem& InsertAction(unsigned int index, const wxString& name, const wxString& caption, const ibPictureDescription& pictureDescription, bool pictureAndText, const ibActionID& lNumAction, bool createInForm = true, ibValue* srcData = nullptr) {
			return EmplaceAction((int)index, lNumAction, name, caption, pictureDescription, pictureAndText, createInForm, srcData);
		}

		void InsertSeparator(unsigned int index) { m_vecAction.insert(m_vecAction.begin() + index, {}); }

		void RemoveAction(const ibActionID& lNumAction) {
			auto iterator = std::find_if(m_vecAction.begin(), m_vecAction.end(), [lNumAction](const ibCommandItem& act) {
				return lNumAction == act.m_actionId; });
			if (iterator != m_vecAction.end()) m_vecAction.erase(iterator);
		}

		// Append every entry (separators included) of another collection onto this one. The reusable copy
		// primitive both the base AppendActionCollection and the list-model GetActionCollection overrides route
		// through, instead of each re-implementing the same id-by-id copy loop.
		void AppendFrom(const ibActionCollection& data) {
			m_vecAction.insert(m_vecAction.end(), data.m_vecAction.begin(), data.m_vecAction.end());
		}

		// ---- id-keyed accessors ----------------------------------------------

		wxString GetNameByID(const ibActionID& lNumAction) const {
			const ibCommandItem* act = FindByID(lNumAction);
			return act != nullptr ? act->m_name : wxEmptyString;
		}

		wxString GetCaptionByID(const ibActionID& lNumAction) const {
			const ibCommandItem* act = FindByID(lNumAction);
			if (act == nullptr)
				return wxEmptyString;
			return act->m_caption.Length() > 0 ? act->m_caption : act->m_name;
		}

		ibPictureDescription GetPictureByID(const ibActionID& lNumAction) const {
			const ibCommandItem* act = FindByID(lNumAction);
			return act != nullptr ? act->m_pictureDescription : ibPictureDescription();
		}

		bool IsCreatePictureAndText(const ibActionID& lNumAction) const {
			const ibCommandItem* act = FindByID(lNumAction);
			return act != nullptr ? act->m_pictureAndText : true;
		}

		bool IsCreateInForm(const ibActionID& lNumAction) const {
			const ibCommandItem* act = FindByID(lNumAction);
			return act != nullptr ? act->m_createInForm : true;
		}

		ibValue* GetSourceDataByID(const ibActionID& lNumAction) const {
			const ibCommandItem* act = FindByID(lNumAction);
			return act != nullptr ? act->m_srcData : nullptr;
		}

		// Does this action modify data? Unknown id → true (assume it does, so a view-only form greys it). Set at
		// add time via the fluent AddAction(...).SetModify(false) — no separate id-keyed setter.
		bool GetModifiesDataByID(const ibActionID& lNumAction) const {
			const ibCommandItem* act = FindByID(lNumAction);
			return act != nullptr ? act->m_modifiesData : true;
		}

		ibActionID GetID(unsigned int idx) const {
			if (idx >= GetCount())
				return wxNOT_FOUND;
			return m_vecAction[idx].m_actionId;
		}

		unsigned int GetCount() const { return (unsigned int)m_vecAction.size(); }
	};

public:

	// Polymorphic base (pure-virtual methods below) — a virtual destructor so deleting a derived object
	// through an ibActionDataObject* runs the derived dtor (else UB / leak). Mostly a mixin (ibValueFrame
	// IS-A one), but correctness shouldn't hinge on nobody ever owning it through this pointer.
	virtual ~ibActionDataObject() = default;

	//support action
	virtual ibActionCollection GetActionCollection(const ibFormID& formType) = 0;

	// execute action
	virtual void CallAsAction(const ibActionID& lNumAction, class ibBackendValueForm* srcForm) = 0;
};

// A simplified, STANDALONE action-analog that lives ON THE MODEL — the model has NO ibActionDataObject of its
// own; it is just a command STORE. Two jobs: (1) hand out its command set (the SAME ibCommandItem the action
// collection uses — one unified record; a default item, m_actionId == wxNOT_FOUND, is a separator), (2) execute a
// command by id against the CURRENT ROW + form. The FRONT (TableBox) merges the set into the real action it
// gives the command bar and routes the execute here with the front-owned row — the model never pulls the widget.
// A command's id carries THIS bit to say "after running me on the model, ALSO start the row's inline editor on
// the front". A model BAKES it into its Edit id in the enum (`eEditValue = <n> | eStartEditingFlag`) — for the MODEL
// it is just an ordinary command value (its own `case eEditValue` carries the bit too). The TableBox's CallAsAction
// tests the bit on EVERY id it forwards to CallAsCommand (which dispatches its Edit case normally — a list opens the
// object form, a value-table does nothing there) and — if the bit is set — runs EditCurrentRow after (a value-table
// / tabular row edits inline; a list no-ops, its form already opened). Inline editing is a pure FRONT operation, not
// round-tripped through the backend to call back.
//
// The id (flag included) MUST stay a valid wxMenuItem id (< 32767): the TableBox appends its command ids straight as
// context-menu item ids (OnContextMenu) — a high bit here trips wxMenuItemBase's `itemid < 32767` assert. So it is a
// FREE bit no real id band sets: model ids (1..27), form chrome (10000..10003), TableBox band (20000..20004) all
// leave bit 12 clear, and `<n> | 0x1000` stays well under 32767 (e.g. eEditValue = 3 | 0x1000 = 4099). A new band
// must likewise keep bit 12 clear.
constexpr ibActionID eStartEditingFlag = 0x1000;

class ibTabularCommandDataObject {
public:
	virtual ~ibTabularCommandDataObject() {}
	virtual void GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const = 0;
	// Runs the command by id against the CURRENT ROW; srcForm is the form that invoked it. (The Edit command DOES
	// arrive here — its id still carries eStartEditingFlag, baked into the model's enum — so a list opens its object
	// form; the front then ALSO opens the inline editor. See eStartEditingFlag above + the TableBox's CallAsAction.)
	virtual void CallAsCommand(const ibActionID& lNumAction, const struct ibDataViewCommandContext& ctx, class ibBackendValueForm* srcForm) = 0;
};

#endif
