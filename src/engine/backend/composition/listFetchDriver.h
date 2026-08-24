#ifndef __LIST_FETCH_DRIVER_H__
#define __LIST_FETCH_DRIVER_H__

// L5-1 — the LIST FETCH driver: ONE output written under the universal
// Get*Fetch triple (GetFirstFetch / GetNextFetch / GetPrevFetch differ only in
// the ENVELOPE — direction / anchor / count; the output is the same).
//
// A stack object built per fetch call: it CARRIES the page envelope in (the
// model builds the ibReadPageRequest exactly as the door path did — anchor,
// direction, the tree's parent filter) and ACCUMULATES meta-keyed rows out
// (attribute metaID -> value, off OutputColumn::GetColumnId()). The list model
// then converts the rows into its own row type — the row guid is pulled from
// the data-reference column's VALUE (the uuid identity column is a raw DB
// column outside the query language; the reference attribute is the named,
// language-visible key carrying the same guid).
//
// The driver knows no metaobject and no row class — it is the passive sink of
// the composer's walk.
//
//   ibListFetchDriver p(page);     // the envelope in
//   m_composer.Run(p);             // render -> parse -> lower -> walk
//   for (auto& row : p.Rows()) …   // the rows out

#include "backend/composition/dataComposer.h"
#include "backend/query/dataQueryBuilder.h"   // ibReadPageRequest — held by value

#include <map>

class BACKEND_API ibListFetchDriver : public ibCompositionDriver
{
public:
	struct Row
	{
		int  m_level = 0;
		// ⭐ CAN THIS ROW BE OPENED — which is NOT "does it have children". It was called
		// `m_hasChildren` while what the driver stores in it is `showsWhatIsUnder`: the base spends a
		// starred paragraph on those being different questions (ibCompositionDriver::OnGroup), and a
		// heading standing over rows this output does not print HAS children and must not offer an
		// expander. The consumers ask it to decide `isContainer`, so the name is now the question.
		bool m_expandable = false;
		std::map<ibMetaID, ibValue> m_values;   // by the attribute metaID

		ibValue GetValue(const ibMetaID& id) const {
			const auto it = m_values.find(id);
			return it != m_values.end() ? it->second : ibValue();
		}
	};

	// Full (single-batch) read — an enum list.
	ibListFetchDriver() = default;

	// Paged read — the envelope the model built (anchor / direction / count + the hierarchy scope, if any).
	// The model fills m_hierarchy* on the request directly (RunComposerPage); there is no separate scope object.
	explicit ibListFetchDriver(const ibReadPageRequest& page) : m_paged(true), m_page(page) {}

	bool GetPageRequest(ibReadPageRequest& request) const override {
		if (!m_paged)
			return false;
		request = m_page;
		return true;
	}

	void OnColumns(const std::vector<ibQueryLowering::OutputColumn>& schema) override {
		m_schema = schema;
		m_rows.clear();
	}

	// A LIST TAKES THE SECOND ANSWER — see the note on ibCompositionDriver::OnGroup. What it does with
	// the flag is draw an expander, and an expander must promise only what the output will actually
	// show; a heading standing over rows this output does not print must not offer to open.
	void OnGroup(int level, bool hasChildren, bool showsWhatIsUnder, const std::vector<ibValue>& values) override {
		OnRow(level, showsWhatIsUnder, values);
	}

	// ⚠ THE SECOND ARGUMENT IS THE BASE'S `hasChildren`, and a DETAIL row answers it truthfully with
	// false. A GROUP row reaches here through OnGroup above, which deliberately hands over the OTHER
	// answer — see Row::m_expandable.
	void OnRow(int level, bool expandable, const std::vector<ibValue>& values) override {
		Row row;
		row.m_level = level;
		row.m_expandable = expandable;
		for (size_t i = 0; i < m_schema.size() && i < values.size(); ++i) {
			const ibBackendQueryColumn* col = m_schema[i].m_col;
			if (col != nullptr)
				row.m_values.emplace(col->GetColumnId(), values[i]);
		}
		m_rows.push_back(std::move(row));
	}

	std::vector<Row>& Rows() { return m_rows; }
	const std::vector<Row>& Rows() const { return m_rows; }

private:
	bool              m_paged = false;
	ibReadPageRequest m_page;

	std::vector<ibQueryLowering::OutputColumn> m_schema;
	std::vector<Row>                           m_rows;
};

#endif
