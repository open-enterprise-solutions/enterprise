#ifndef __WEB_TABLE_BOX_H__
#define __WEB_TABLE_BOX_H__

// The web render shims for a tablebox and the two node kinds that live
// under it — a column and a group of columns.
//
// They replace the ibWebStubControl placeholders these three controls
// used to return from Create(). A stub carries a type string and
// nothing else, so a column reached the browser as
// {"type":"tableboxcolumn","label":""} — no caption, no width, no
// alignment, no value type — and the table carried no rows at all.
// There was nothing for a renderer to draw even in principle.
//
// Two roads, deliberately separate:
//
//   * the shape of it — this table, its columns and their look —
//     travels with the form JSON, pushed here by the controls' own
//     Update / OnUpdated, exactly as a button pushes its caption;
//   * the rows travel on their own, one page per request, through
//     ibWebTableBox::FetchPage. A list is paged by architecture (see
//     ibValueModel::GetFirstFetch / GetNextFetch / GetPrevFetch), and
//     folding a page into the form tree would have thrown that away.
//
// The rows the table has handed out are remembered here, because the
// browser cannot hold an ibDataViewItem: it is a refcounted handle on
// a live row. The client addresses a row by a key this window minted
// for it — a key stays valid as long as the window does, so a page
// fetched later does not renumber the rows already on screen. The
// server keeps the ends of the window, which is what paging anchors
// from.

#include <vector>

#include "backend/tabularModelView.h"   // ibDataViewItem — the page anchors

#include "webWindow.h"

struct ibTypeDescription;

class ibValueModelTableBox;
class ibValueModelTableBoxColumn;

// ---------------------------------------------------------------------------
// A column — a header and a promise about what its cells hold.
// ---------------------------------------------------------------------------
class ibWebTableBoxColumn : public ibWebWindow {
public:
	explicit ibWebTableBoxColumn(int id = 0) : ibWebWindow(id) {}

	virtual wxString GetControlType() const override { return wxT("tableboxcolumn"); }

	void SetCaption(const wxString& caption)   { m_caption = caption; }
	void SetFieldKey(const wxString& key)      { m_field = key; }
	void SetWidth(int width)                   { m_width = width; }
	void SetAlign(const wxString& align)       { m_align = align; }
	void SetHeaderAlign(const wxString& align) { m_headerAlign = align; }
	void SetValueType(const wxString& type)    { m_valueType = type; }
	void SetVisibleColumn(bool visible)        { m_visible = visible; }
	void SetResizable(bool resizable)          { m_resizable = resizable; }
	void SetReadOnly(bool readOnly)            { m_readOnly = readOnly; }
	// Whether a click on this header sorts, and which way it sorts now:
	// "none" until the composer says otherwise, then "asc" or "desc".
	void SetSortable(bool sortable)            { m_sortable = sortable; }
	void SetSortOrder(const wxString& order)   { m_sortOrder = order; }

	const wxString& GetFieldKey() const { return m_field; }

	virtual nlohmann::json ToJSON() const override;

private:
	wxString m_caption;
	wxString m_field;
	wxString m_align       = wxT("left");
	wxString m_headerAlign = wxT("left");
	wxString m_valueType   = wxT("string");
	int      m_width     = 80;
	wxString m_sortOrder = wxT("none");
	bool     m_visible   = true;
	bool     m_resizable = true;
	bool     m_readOnly  = true;   // iteration 2 is read-only throughout
	bool     m_sortable  = false;
};

// ---------------------------------------------------------------------------
// A group of columns — a title in the header and a decision about which way
// its members run. It renders nothing of its own in the rows.
// ---------------------------------------------------------------------------
class ibWebTableBoxColumnGroup : public ibWebWindow {
public:
	explicit ibWebTableBoxColumnGroup(int id = 0) : ibWebWindow(id) {}

	virtual wxString GetControlType() const override { return wxT("tableboxcolumngroup"); }

	void SetCaption(const wxString& caption) { m_caption = caption; }
	void SetGrouping(const wxString& kind)   { m_grouping = kind; }
	void SetShowTitle(bool show)             { m_showTitle = show; }
	void SetAlign(const wxString& align)     { m_align = align; }
	void SetVisibleGroup(bool visible)       { m_visible = visible; }

	virtual nlohmann::json ToJSON() const override;

private:
	wxString m_caption;
	wxString m_grouping  = wxT("vertical");
	wxString m_align     = wxT("center");
	bool     m_showTitle = false;
	bool     m_visible   = true;
};

// ---------------------------------------------------------------------------
// The table itself.
// ---------------------------------------------------------------------------
class ibWebTableBox : public ibWebWindow {
public:
	explicit ibWebTableBox(int id = 0) : ibWebWindow(id) {}

	virtual wxString GetControlType() const override { return wxT("tablebox"); }

	void SetShowHeader(bool show)          { m_header = show; }
	void SetShowFooter(bool show)          { m_footer = show; }
	void SetViewMode(const wxString& mode) { m_viewMode = mode; }
	void SetChoiceMode(bool choice)        { m_choiceMode = choice; }
	void SetPageSize(int size)             { m_pageSize = size > 0 ? size : 1; }

	int GetPageSize() const { return m_pageSize; }

	virtual nlohmann::json ToJSON() const override;

	// One page of rows, read through the control's model.
	//
	// The control is passed IN rather than remembered: the dispatcher
	// that answers the HTTP request resolves it anyway (form ->
	// FindControlByID -> host->GetWxObject), so both are alive for the
	// length of the call by construction, and this shim never holds a
	// pointer whose owner outlives or predeceases it.
	//
	// `dir` is "first" | "next" | "prev". Anything else reads as
	// "first", which is also what next/prev fall back to with no page
	// in hand.
	nlohmann::json FetchPage(ibValueModelTableBox* control,
		const wxString& dir, int count);

	// Three kinds. Two carry a row key:
	//   "row"      — the client moved the cursor onto that row;
	//   "activate" — it opened that row (a double-click), which for a
	//                list means raising the object's own form.
	// The third carries a COLUMN's control id:
	//   "sort"     — it clicked that header. Sorting a list is an ORDER BY
	//                over the whole table, so it is committed to the
	//                composer and the list is read again from the top.
	virtual bool HandleRequest(const wxString& kind,
		const wxString& value) override;

	// Set by the dispatcher before HandleRequest: a cursor move has to
	// reach the CONTROL (ApplyCurrentLine lives there), and the shim is
	// deliberately without a back-pointer of its own.
	void SetRequestControl(ibValueModelTableBox* control) { m_requestControl = control; }

private:
	// Re-read the composer's order onto the column nodes.
	//
	// Every other property reaches a node through its control's Update, and
	// the tree is serialised from the nodes without running that again. A
	// sort goes straight to the composer and touches no control, so without
	// this the rows come back in the new order under an arrow still pointing
	// the old way.
	void SyncSortOrders(ibValueModelTableBox* control);

	// Window key of the control's current line, -1 when it has none or
	// the line is outside the window in hand.
	int CurrentKey(ibValueModelTableBox* control) const;

private:
	// One row of the window the client currently holds: the key it was
	// handed out under, and the row that key stands for.
	struct WindowRow {
		int             key;
		ibDataViewItem  item;
	};

	// The window, in display order. Its front and back are what prev /
	// next page from. "first" throws it away and starts again; nothing
	// else renumbers it.
	std::vector<WindowRow> m_window;
	int                    m_nextKey = 0;

	// A window has to stop growing somewhere: every row in it pins a
	// live row of the model. Past this the table answers "no more" and
	// the client can ask for the list again from the top — which is
	// also the only honest thing a page showing five thousand rows can
	// be told.
	static const int kWindowLimit = 5000;

	ibValueModelTableBox* m_requestControl = nullptr;   // borrowed, for one call

	wxString m_viewMode   = wxT("hierarchical");
	bool     m_header     = true;
	bool     m_footer     = false;
	bool     m_choiceMode = false;
	int      m_pageSize   = 50;
};

// The key a cell is filed under in a fetched row, and the same string a
// column publishes as its "field". Derived from the column's control id
// on both sides, so the two agree by construction rather than by lookup.
wxString ibWebTableBoxColumnKey(int controlId);

// wxALIGN_LEFT / _CENTER / _RIGHT and ibColumnGroupKind, as the browser
// says them. Here rather than at the callsites so the two control files
// that push these do not each invent a spelling.
wxString ibWebAlignName(int alignment);
wxString ibWebGroupingName(int kind);

// What a column's cells hold, from the type its binding declares. The
// browser needs the KIND, not the class: a number right-aligns, a date
// formats, a reference is a link once it can be followed.
wxString ibWebValueTypeName(const ibTypeDescription& type);

#endif
