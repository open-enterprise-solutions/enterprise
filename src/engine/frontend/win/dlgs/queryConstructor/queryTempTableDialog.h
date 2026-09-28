#ifndef __QUERY_TEMP_TABLE_DIALOG_H__
#define __QUERY_TEMP_TABLE_DIALOG_H__

////////////////////////////////////////////////////////////////////////////
// A TABLE WITH NO ROWS YET, DESCRIBED — its name and the type of each of its fields.
////////////////////////////////////////////////////////////////////////////
//
// Two tables have no rows while a query is written: one handed in as a parameter — `&Goods`, a value table
// given when the query runs — and a temporary table the temporary tables manager brings — `Goods`, with no
// ampersand, made by nobody in this package. The ampersand says which, as it does for any value (Max,
// 2026-09-28). Neither can tell the constructor what fields it has, so this window says them — and says them
// IN THE TEXT, as fields of the query that reads the table, each CAST to its type:
//
//     SELECT CAST(Goods.Qty AS Number(15, 2)) AS Qty, CAST(Goods.Item AS Catalog.Items) AS Item
//     FROM &Goods
//
// It is a table of the query being edited, not a statement of its own: the package's statements are the
// author's to add. Nothing is kept beside the text — opened again on the table, the window reads the fields
// back (Read); the model reads the same CASTs to unfold the table (ibQueryConstructorModel::FieldsTakenFrom);
// the engine converts each value to its type when the query runs. One sentence, three readers.
//
// A field whose type no CAST can say (a composite one) is written uncast, and comes back untyped.
//
////////////////////////////////////////////////////////////////////////////

#include "frontend/frontend.h"

#include <wx/dialog.h>

#include <vector>

#include "backend/query/queryAST.h"
#include "backend/typeDescription.h"

#include "frontend/win/ctrls/dataview/dataview.h"

class ibMetaData;
class ibQueryConstructorModel;

class FRONTEND_API ibDialogQueryTempTable : public wxDialog
{
public:
	struct Field
	{
		wxString          m_name;
		ibTypeDescription m_type;   // empty = not said: the field is written uncast
	};

	// The table's name as the window shows it: `&Goods` for one handed in, `Goods` for a temporary table.
	static wxString NameOf(const ibQuerySource& source);
	// WHAT `reader` SAYS OF THE TABLE `source` — its fields, as the select takes them (FieldsTakenFrom).
	static std::vector<Field> Read(const ibQuerySelect& reader, const ibQuerySource& source,
	                              const ibQueryConstructorModel& model);
	// …and `reader` saying it anew: the fields it took from `source` give way to `fields`, each CAST to its type,
	// where the first of them stood (at the end when there was none), and `source` takes `name` — `&Name` a table
	// handed in, a bare `Name` the temporary table of that name. Whatever else the select wrote against the old
	// name follows it.
	static void Write(ibQuerySelect& reader, ibQuerySource& source, const ibQueryConstructorModel& model,
	                  const wxString& name, const std::vector<Field>& fields);

	ibDialogQueryTempTable(wxWindow* parent, const wxString& name, std::vector<Field> fields,
	                       const ibQueryConstructorModel& model, const ibMetaData* metaData,
	                       bool readOnly = false);

	wxString                  GetTableName() const;
	const std::vector<Field>& GetFields() const { return m_fields; }

private:
	void OnAdd(wxCommandEvent&);
	void OnRemove(wxCommandEvent&);
	void MoveField(int delta);
	void OnOk(wxCommandEvent&);
	void ShowFields();
	long SelectedRow() const;

	// A TYPE IN THE CELL is written as the text writes it after AS — `Number(15, 2)`, `Catalog.Items` —
	// and read back by the parser, so the cell can be typed into and says nothing the query could not.
	wxString TypeText(const ibTypeDescription& type) const;
	bool     ReadType(const wxString& text, ibTypeDescription& type) const;
	// The "..." — the product's type picker, one type.
	bool     PickType(wxString& text);

	const ibQueryConstructorModel& m_model;
	const ibMetaData*              m_metaData = nullptr;
	bool                           m_readOnly = false;

	std::vector<Field> m_fields;

	class wxTextCtrl*       m_nameBox = nullptr;
	ibDataViewCtrl*         m_grid    = nullptr;
	class ibQueryGridModel* m_rows    = nullptr;
};

#endif // __QUERY_TEMP_TABLE_DIALOG_H__
