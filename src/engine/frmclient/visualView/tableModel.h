#ifndef _FRMCLIENT_VIEW_TABLE_MODEL_H__
#define _FRMCLIENT_VIEW_TABLE_MODEL_H__

#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "frmclient/visualView/ctrl/frame.h"
#include "frmclient/win/ctrls/dataview/tabularModelView.h"

// A CELL of a table's row as the server wrote it: the text, a tick for a boolean, the right side for a number — and
// how it looks, the conditional appearance the server's model gave it.
struct ibViewCell {
	wxString            text;
	std::optional<bool> checked;
	bool                number = false;
	bool                editable = false;   // the server wrote its value: a cell it lets be edited
	ibDataViewItemAttr  look;
};

// A ROW of a table as the server handed it out — named by its handle, which stays the row's while it is handed out;
// its cells by the columns' ids.
class ibViewTableRow : public ibDataViewObject {
public:

	ibViewTableRow(long long handle, const ibDataViewItem& parent) : m_handle(handle), m_parent(parent) {}

	// The same row — the same handle, whichever read made the object.
	virtual bool IsEqualTo(const ibDataViewObject& other) const override;
	virtual bool IsContainer() const override { return m_container; }
	virtual bool GetGroupCaption(wxString& caption) const override;
	virtual ibDataViewItem GetParentItem() const override { return m_parent; }

	long long GetHandle() const { return m_handle; }
	const ibViewCell* FindCell(long long column) const;
	ibPictureID GetPicture() const { return m_picture; }

	// The row from its node of a fetch's answer.
	void Read(const ibProtocolNode& row);

private:

	const long long      m_handle;
	const ibDataViewItem m_parent;   // the row it was fetched under — none: the top
	bool                 m_container = false;
	wxString             m_group;    // a group heading's caption
	ibPictureID          m_picture = 0;

	std::map<long long, ibViewCell> m_cells;
};

// THE TABLE'S ROWS — the data view's model over `fetch`: a portion of rows in either direction from a row it has, or
// read anew (Reset) around the row the server has current. It keeps no rows: the data view holds them, and reads
// them on its own thread (ibDataViewModel's fetch thread) — so the model speaks to the communicator alone (a fetcher),
// and what the UI must do with an answer is handed to it after the read (`answered`).
class ibViewTableModel : public ibDataViewModel {
public:

	// What an answer leaves the UI to do — the current row it named, the pictures it carried (id, base64), drawn there:
	// a wx image is made on the UI thread only.
	struct ibAnswer {
		long long                                     currentRow = 0;
		std::vector<std::pair<ibPictureID, wxString>> pictures;
	};
	using ibAnswered = std::function<void(const ibAnswer& answer)>;

	ibViewTableModel(ibViewFetcher fetcher, ibAnswered answered);

	// A row's handle — 0: no row of this table.
	static long long HandleOf(const ibDataViewItem& item);
	// May the cell be edited — the server wrote its value.
	static bool IsCellEditable(const ibDataViewItem& item, long long column);

	// A cell — the model's column is the column's id (ibValueModelTableBoxColumn builds it so): a boolean as itself, anything else
	// as the text the server wrote it in; a number stands right by its look. It has a value where the server wrote one: a
	// folder's only in the first column.
	virtual void GetValue(wxVariant& variant, const ibDataViewItem& item, unsigned int col) const override;
	virtual bool HasValue(const ibDataViewItem& item, unsigned col) const override;
	virtual bool GetAttr(const ibDataViewItem& item, unsigned int col, ibDataViewItemAttr& attr) const override;
	virtual bool SetValue(const wxVariant& variant, const ibDataViewItem& item, unsigned int col) override;
	virtual ibDataViewItem GetParent(const ibDataViewItem& item) const override;

	virtual unsigned int GetFirstFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor, int count,
		ibDataViewItemArray& out) const override;
	virtual unsigned int GetNextFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor, int count,
		ibDataViewItemArray& out) const override;
	virtual unsigned int GetPrevFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor, int count,
		ibDataViewItemArray& out) const override;

	virtual bool IsPagedModel() const override { return true; }
	virtual bool HasKeyedRows() const override { return true; }

	virtual ibPictureID GetRowPicture(const ibDataViewItem& item) const override;

private:

	// A row of this table — every object its items hold is one it read, or one standing for one (the reference
	// model's GetViewData).
	static const ibViewTableRow* RowOf(const ibDataViewItem& item);

	// A portion read — Direction 0 reset, 1 forward, -1 backward; the rows in `out`, how many read.
	unsigned int Fetch(int direction, const ibDataViewItem& parent, const ibDataViewItem& anchor, int count,
		ibDataViewItemArray& out) const;

	const ibViewFetcher m_fetcher;
	const ibAnswered    m_answered;
};

#endif
