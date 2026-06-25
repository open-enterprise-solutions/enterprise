#ifndef __SRC_EXPLORER_H__
#define __SRC_EXPLORER_H__

#include "backend/query/queryColumn.h"   // ibBackendQueryColumn / ibBackendSourceColumn — neutral column

// ibSourceExplorer — a source's column/field TEMPLATE (one-time form generation + the picker).
// METADATA-FREE: a node holds plain values (name / synonym / id / type) + UI flags, and a column is
// appended from the neutral ibBackendQueryColumn (a metaobject attribute IS one, a queryable column
// IS one) — never a metaobject pointer. The flags that used to be read off the metaobject (allowed /
// visible / table-section) are now explicit on the node; the allowed-judgement lives on the column
// (ibBackendSourceColumn::IsAllowed). One root + its children; a table-section child carries its own
// columns (the only two-level case the form builder consumes).
class BACKEND_API ibSourceExplorer {

	struct ibSourceInfo {
		wxString          m_srcName;
		wxString          m_srcSynonym;
		ibMetaID          m_mid = wxNOT_FOUND;
		ibTypeDescription m_typeDesc;
		bool              m_enabled = true;
		bool              m_visible = true;
		bool              m_tableSection = false;
		bool              m_select = true;
	};

public:

	ibSourceExplorer() {}

	// The one value ctor — every node is built from plain values + flags, no metaobject.
	ibSourceExplorer(const wxString& name, const wxString& synonym, const ibMetaID& id,
		const ibTypeDescription& typeDesc, bool tableSection = false, bool select = true,
		bool enabled = true, bool visible = true) {
		m_sourceInfo = { name, synonym, id, typeDesc, enabled, visible, tableSection, select };
	}

	// Queryable-style node: the name doubles as the synonym.
	ibSourceExplorer(const wxString& name, const ibMetaID& id, const ibTypeDescription& typeDesc)
		: ibSourceExplorer(name, name, id, typeDesc) {}

	wxString GetSourceName()    const { return m_sourceInfo.m_srcName; }
	wxString GetSourceSynonym() const { return m_sourceInfo.m_srcSynonym; }
	ibMetaID GetSourceId()      const { return m_sourceInfo.m_mid; }

	bool IsEnabled()      const { return m_sourceInfo.m_enabled; }
	bool IsVisible()      const { return m_sourceInfo.m_visible; }
	bool IsTableSection() const { return m_sourceInfo.m_tableSection; }
	bool IsSelect()       const { return m_sourceInfo.m_select; }

	const std::vector<ibClassID>& GetClsidList() const { return m_sourceInfo.m_typeDesc.GetClsidList(); }
	bool ContainType(const ibValueTypes& valType) const { return m_sourceInfo.m_typeDesc.ContainType(valType); }
	bool ContainType(const ibClassID& cid) const { return m_sourceInfo.m_typeDesc.ContainType(cid); }

	// Append a COLUMN from its neutral descriptor (an attribute / a queryable column — both
	// ibBackendQueryColumn). Skipped when the column is not allowed (a deleted / disabled field):
	// the metadata judgement lives on the column, not here.
	void AppendColumn(const ibBackendQueryColumn* col, bool enabled = true, bool visible = true) {
		if (col == nullptr || !col->IsAllowed()) return;
		m_arraySource.emplace_back(ibSourceExplorer{ col->GetName(), col->GetSynonym(), col->GetColumnId(),
			col->GetTypeDesc(), /*tableSection*/false, /*select*/true, enabled, visible });
	}

	// Append a COLUMN by plain values (a queryable column with no descriptor object).
	void AppendColumn(const wxString& name, const ibMetaID& id, const ibTypeDescription& typeDesc) {
		m_arraySource.emplace_back(ibSourceExplorer{ name, id, typeDesc });
	}

	// Append a TABLE-SECTION node (tableSection = true). Returns a reference so the caller adds the
	// section's own columns to it via AppendColumn (the form builder reads them as a sub-tablebox).
	ibSourceExplorer& AppendTable(const wxString& name, const wxString& synonym, const ibMetaID& id,
		const ibTypeDescription& typeDesc) {
		m_arraySource.emplace_back(ibSourceExplorer{ name, synonym, id, typeDesc, /*tableSection*/true });
		return m_arraySource.back();
	}

	ibSourceExplorer GetHelper(unsigned int idx) const {
		if (idx >= m_arraySource.size())
			return ibSourceExplorer();
		return m_arraySource[idx];
	}

	unsigned int GetHelperCount() const { return static_cast<unsigned int>(m_arraySource.size()); }

protected:

	ibSourceInfo m_sourceInfo;
	std::vector<ibSourceExplorer> m_arraySource;
};

#include "backend/srcObject.h"

#endif
