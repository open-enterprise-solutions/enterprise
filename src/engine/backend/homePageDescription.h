#ifndef __HOME_PAGE_DESCRIPTION_H__
#define __HOME_PAGE_DESCRIPTION_H__

#include "backend/backend_core.h"   // ibMetaID, wxNOT_FOUND
#include <vector>

// Home-page workspace description — WHAT the start page shows and HOW it is split.
//
// The workspace is owned by the CONFIG ROOT (ibValueMetaObjectConfiguration): it is the one
// surface every session opens before anything else, so its description travels with the
// configuration, not with a user setting. The description is deliberately DUMB — a template
// (how many columns and in which ratio) plus, per column, an ordered list of FORM ids. It
// holds no widgets and no runtime: the desktop composite doc/view (ibHomePageDocument) reads
// it and builds the splitters; a headless / web host can read the same description and lay
// the same forms out its own way.
//
// A single item is a metaobject FORM id (ibValueMetaObjectFormBase — a common form, or a
// form of a Catalog / Document / Report / …). Opening a list form yields the list; opening
// an object form yields a form over a NEW object of that type — the same thing the
// metaobject's own command does, so nothing here is form-kind aware.

enum ibHomePageTemplate {
	eHomePageTemplate_OneColumn = 0,        // everything under one another, one column
	eHomePageTemplate_TwoEqualColumns,      // 1 : 1
	eHomePageTemplate_TwoColumnsWideLeft,   // 2 : 1
	eHomePageTemplate_TwoColumnsWideRight,  // 1 : 2
};

enum ibHomePageColumn {
	eHomePageColumn_Left = 0,
	eHomePageColumn_Right,
	eHomePageColumn_Count
};

// One attached form: WHICH form, how much vertical room it asks for, and whether it renders.
// m_height is a PROPORTION (a weight), not pixels — 0 means "share the column equally with
// the other zero-weight items", which is what the designer's "same height" checkbox writes.
struct ibHomePageItem {
	ibMetaID m_formId = wxNOT_FOUND;
	unsigned int m_height = 0;
	bool m_visible = true;

	ibHomePageItem() {}
	ibHomePageItem(const ibMetaID& formId, unsigned int height = 0, bool visible = true)
		: m_formId(formId), m_height(height), m_visible(visible) {}

	bool IsOk() const { return m_formId != wxNOT_FOUND; }

	bool operator==(const ibHomePageItem& o) const {
		return m_formId == o.m_formId && m_height == o.m_height && m_visible == o.m_visible;
	}
	bool operator!=(const ibHomePageItem& o) const { return !(*this == o); }
};

class BACKEND_API ibHomePageDescription {
public:

	ibHomePageDescription() {}

	bool operator==(const ibHomePageDescription& o) const {
		return m_template == o.m_template
			&& m_columns[eHomePageColumn_Left] == o.m_columns[eHomePageColumn_Left]
			&& m_columns[eHomePageColumn_Right] == o.m_columns[eHomePageColumn_Right];
	}
	bool operator!=(const ibHomePageDescription& o) const { return !(*this == o); }

	ibHomePageTemplate GetTemplate() const { return m_template; }
	void SetTemplate(ibHomePageTemplate value) { m_template = value; }

	// A one-column template folds the right column INTO the left one on read, so a template
	// switch never loses forms — they just stop being addressed as a second column.
	bool IsTwoColumns() const { return m_template != eHomePageTemplate_OneColumn; }

	// Width share of the LEFT column, (0..1). The composite feeds it to the column splitter
	// as its sash gravity, so the ratio survives a resize.
	double GetColumnGravity() const {
		switch (m_template) {
		case eHomePageTemplate_TwoColumnsWideLeft:  return 2.0 / 3.0;
		case eHomePageTemplate_TwoColumnsWideRight: return 1.0 / 3.0;
		default: return 0.5;
		}
	}

	std::vector<ibHomePageItem>& GetColumn(ibHomePageColumn column) { return m_columns[column]; }
	const std::vector<ibHomePageItem>& GetColumn(ibHomePageColumn column) const { return m_columns[column]; }

	// The forms a column actually renders — visible items with a resolvable id. This is the
	// list the composite lays out; everything else in the description is designer state.
	std::vector<ibHomePageItem> GetShownItems(ibHomePageColumn column) const;

	void AppendItem(ibHomePageColumn column, const ibHomePageItem& item) { m_columns[column].push_back(item); }
	bool RemoveItem(ibHomePageColumn column, unsigned int index);
	bool MoveItem(ibHomePageColumn column, unsigned int index, int offset);

	// node form — one Child sub-node { Template, Left[], Right[] }. The owner (the config
	// root) stores it as a single named property, so adding a field here is forward-compatible.
	bool ReadNode(const class ibDataValue& value);
	bool WriteNode(class ibDataValue& value) const;

private:

	ibHomePageTemplate m_template = eHomePageTemplate_TwoEqualColumns;
	std::vector<ibHomePageItem> m_columns[eHomePageColumn_Count];
};

#endif // !__HOME_PAGE_DESCRIPTION_H__
