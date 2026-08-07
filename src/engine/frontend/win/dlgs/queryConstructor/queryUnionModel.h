#ifndef __QUERY_UNION_MODEL_H__
#define __QUERY_UNION_MODEL_H__

////////////////////////////////////////////////////////////////////////////
// The UNIONS tab's two models — the branches, and the field map across them.
////////////////////////////////////////////////////////////////////////////
//
// A union stacks selects vertically, and the whole question it raises is: WHICH COLUMN OF EACH
// BRANCH lines up with which output field. Our lowering answers it BY NAME (the union's output is
// the first branch's columns, read back by name), so the map is not a setting to store — it is a
// TABLE OF WHAT ALREADY LINES UP, one row per output field, one column per branch.
//
// That is why the map has no SetValue: `<none>` in a cell is not something to fix here, it is the
// branch saying it has no column of that name. Fixing it means going to that branch's Fields tab
// and naming one — which is where field names are decided for every other purpose too.
//
// The branch list is the editable half: a name (read off the branch, not stored) and the one
// setting a branch carries — whether it keeps duplicates (UNION ALL) — toggled in place.
//
////////////////////////////////////////////////////////////////////////////

#include "frontend/win/ctrls/dataview/dataview.h"

#include "backend/query/queryAst.h"

#include <functional>
#include <vector>

// Column 0 is reserved by the ibDataViewCtrl fork.
enum ibQueryUnionColumn {
	kUnionColName = 1,
	kUnionColKeepDuplicates,
};

// The branch list: "Query 1" … and its "keep duplicates" box.
class ibQueryUnionModel : public ibDataViewVirtualListModel
{
public:
	ibQueryUnionModel() = default;

	// The select being edited — BORROWED. Row 0 IS this query (a union's first branch is the query
	// itself); rows 1..n are m_unions.
	void SetContent(ibQuerySelect* select);

	void SetOnChanged(std::function<void()> onChanged) { m_onChanged = std::move(onChanged); }

	// The branch a row stands for: null for row 0 (that is the select itself).
	ibQuerySelect* BranchAt(unsigned int row) const;

	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override;
	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override;
	bool IsEnabledByRow(unsigned row, unsigned col) const override;

private:
	ibQuerySelect*        m_select = nullptr;
	std::function<void()> m_onChanged;
};

// The field map: one row per output field, one column per branch. Columns are added by the dialog
// as branches appear, so the model answers by INDEX — column N is branch N.
class ibQueryUnionFieldModel : public ibDataViewVirtualListModel
{
public:
	ibQueryUnionFieldModel() = default;

	void SetContent(ibQuerySelect* select);

	// The output field names — the first branch's, because that is what the union's result IS.
	const std::vector<wxString>& FieldNames() const { return m_names; }
	unsigned int BranchCount() const { return m_branchCount; }

	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override;
	// The field NAME is the alias — writing it names the field of the RESULT.
	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override;

	void SetOnChanged(std::function<void()> onChanged) { m_onChanged = std::move(onChanged); }
	// WHY A RENAME WAS REFUSED, said AT THE MOMENT it is refused. A cell that silently keeps its old
	// text teaches nothing — the author retypes the same thing and gets the same nothing.
	void SetOnError(std::function<void(const wxString&)> onError) { m_onError = std::move(onError); }

private:
	std::function<void()> m_onChanged;
	std::function<void(const wxString&)> m_onError;
	// What a branch offers under `name`, as the text a reader recognises, or empty when it has none.
	wxString ColumnOf(const ibQuerySelect& branch, const wxString& name) const;

	ibQuerySelect*        m_select = nullptr;
	std::vector<wxString> m_names;
	unsigned int          m_branchCount = 0;
};

#endif // __QUERY_UNION_MODEL_H__
