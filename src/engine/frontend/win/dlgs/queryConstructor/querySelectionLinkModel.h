#ifndef __QUERY_SELECTION_LINK_MODEL_H__
#define __QUERY_SELECTION_LINK_MODEL_H__

////////////////////////////////////////////////////////////////////////////
// THE SELECTION-LINKS GRID — a dataview model over `ibQueryPackage::m_links`.
////////////////////////////////////////////////////////////////////////////
//
// One row per link the PACKAGE declares between two of its NAMED results (`ONTO`). Five cells, the
// same five a join is read through: SELECTION · all · SELECTION · all · CONDITION.
//
// ⭐⭐ WHY THIS IS NOT ibQueryLinkModel WITH A DIFFERENT SOURCE (Max, 2026-08-21). That one edits the
// JOINS OF ONE STATEMENT — a link there IS a join, so naming a table on either side puts that table
// into the statement's FROM. Here nothing of the sort may happen: the author marks two statements as
// named selections and declares the relation BETWEEN them, and no statement's tables are touched,
// nothing is materialised, nothing is substituted. Same five cells, opposite effect on the query —
// so it is its own model rather than the other one with a flag.
//
// The two checkboxes are the join KIND, exactly as on the Links tab: neither → Inner, left → Left,
// right → Right, both → Full. The kind lives in the link as an `ibQueryJoinKindAst`; nothing here
// stores a pair of bools.
//
////////////////////////////////////////////////////////////////////////////

#include "frontend/win/ctrls/dataview/dataview.h"
#include "queryLinkModel.h"                     // the column ids — one grid shape, one set of ids

#include "backend/query/queryAst.h"

#include <functional>

class ibQuerySelectionLinkModel : public ibDataViewVirtualListModel
{
public:
	ibQuerySelectionLinkModel() = default;

	// The package being edited (BORROWED — the model never owns or copies it, which is what makes
	// "the grid and the text say the same thing" true by construction).
	void SetContent(ibQueryPackage* package);

	// Raised after a cell changed the package, so the dialog refills everything and re-renders text.
	void SetOnChanged(std::function<void()> onChanged) { m_onChanged = std::move(onChanged); }
	// Where the ENGINE's complaint goes when a condition typed into the cell does not parse.
	void SetOnError(std::function<void(const wxString&)> onError) { m_onError = std::move(onError); }

	// A row is a LINK — add one (empty, to be filled in the cells) and remove the one under the
	// cursor. Nothing else in the package changes.
	void AddLink();
	void RemoveLink(unsigned int row);

	ibQueryPackageLink* LinkAt(unsigned int row) const;

	// ---- ibDataViewListModel ----
	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override;
	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override;

private:
	ibQueryPackage*       m_package = nullptr;   // borrowed
	std::function<void()> m_onChanged;
	std::function<void(const wxString&)> m_onError;
};

#endif // __QUERY_SELECTION_LINK_MODEL_H__
