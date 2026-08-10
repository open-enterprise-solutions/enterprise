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

	// THE PICTURE FOR A ROW, answered by the host (which can reach the model that reaches the
	// column). The union's rows are output FIELDS; drawing them without the picture every other
	// field list shows made the same thing look like two different things in two tabs.
	void SetIconReader(std::function<wxIcon(unsigned int row)> reader) { m_iconReader = std::move(reader); }

	void SetOnChanged(std::function<void()> onChanged) { m_onChanged = std::move(onChanged); }
	// WHY A RENAME WAS REFUSED, said AT THE MOMENT it is refused. A cell that silently keeps its old
	// text teaches nothing — the author retypes the same thing and gets the same nothing.
	void SetOnError(std::function<void(const wxString&)> onError) { m_onError = std::move(onError); }

	// ⭐ WHICH BRANCH A COLUMN IS, and the branch itself. Column 0 is the alias; column N + 1 is
	// branch N, where branch 0 is THIS query and the rest are the unions.
	ibQuerySelect* BranchSelectAt(unsigned int branch) const;

	// The fields that branch could supply — asked of the host, which is the only one that can reach
	// the metadata. Used to fill the branch cells' drop-downs: a union is LINED UP by hand here.
	using BranchFields = std::function<wxArrayString(unsigned int branch)>;
	void SetBranchFields(BranchFields fields) { m_branchFields = std::move(fields); }
	wxArrayString FieldsOfBranch(unsigned int branch) const {
		return m_branchFields ? m_branchFields(branch) : wxArrayString();
	}

private:
	std::function<void()> m_onChanged;
	std::function<void(const wxString&)> m_onError;
	std::function<wxIcon(unsigned int)>  m_iconReader;   // the row's own picture, asked of the host
	// What a branch offers under `name`, as the text a reader recognises, or empty when it has none.
	wxString ColumnOf(const ibQuerySelect& branch, const wxString& name) const;
	// Point a branch at one of its own fields for this output row; empty UNLINES the field it held —
	// the field keeps its expression and moves to a row of its own rather than being destroyed.
	bool SetBranchColumn(unsigned int row, unsigned int branch, const wxString& field);
	// The wanted name if no branch is using it, else the same with a number — `Code`, `Code1`, …
	wxString FreeOutputName(const wxString& wanted) const;

	ibQuerySelect*        m_select = nullptr;
	std::vector<wxString> m_names;
	unsigned int          m_branchCount = 0;
	BranchFields          m_branchFields;
};

#endif // __QUERY_UNION_MODEL_H__
