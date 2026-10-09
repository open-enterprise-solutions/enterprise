#ifndef __LIST_FETCH_DRIVER_H__
#define __LIST_FETCH_DRIVER_H__

// L5-1 — the LIST FETCH driver: ONE output written under the universal
// Get*Fetch triple (GetFirstFetch / GetNextFetch / GetPrevFetch differ only in
// the ENVELOPE — direction / anchor / count; the output is the same).
//
// A stack object built per fetch call: it CARRIES the page envelope in (the
// model builds the ibReadPageRequest exactly as the door path did — anchor,
// direction, the tree's parent filter) and ACCUMULATES rows out keyed by the
// READER's column ids — asked of the reader by the output's NAME (see the paged
// constructor), else the output column's own id. The list model
// then converts the rows into its own row type — the row guid is pulled from
// the data-reference column's VALUE (the uuid identity column is a raw DB
// column outside the query language; the reference attribute is the named,
// language-visible key carrying the same guid).
//
// The driver knows no metaobject and no row class — it is the passive sink of
// the composer's walk.
//
//   ibListFetchDriver p(page, idOf);   // the envelope in, and the reader's column lookup
//   m_composer.Run(p);             // render -> parse -> lower -> walk
//   for (auto& row : p.Rows()) …   // the rows out

#include "backend/composition/drivers/compositionDriver.h"   // a DRIVER needs the contract, not the composer
#include "backend/query/dataQueryBuilder.h"   // ibReadPageRequest — held by value

#include <functional>
#include <map>
#include <memory>   // a row's conditional appearance, null where there is none — see Row::m_attr

class BACKEND_API ibListFetchDriver : public ibCompositionDriver
{
public:
	struct Row
	{
		int  m_level = 0;
		// ⭐⭐ …AND HOW DEEP INSIDE THAT RUNG. A rung that unfolds a hierarchy recurses WITHIN itself, so
		// two facts are needed to say where a heading stands and one of them used to be dropped here on
		// the grounds that "a list draws its own tree from the nodes it is handed". That was true while
		// the tree came from a lazy parent-scope drill; it stops being true the moment a list and a
		// report are one construction, because then the list's tree is the FOLD's tree and its depth is
		// this number (Max, 2026-08-29: *"the report and the list give the same result under groupings"*).
		int  m_indent = 0;
		// ⭐ CAN THIS ROW BE OPENED — which is NOT "does it have children". It was called
		// `m_hasChildren` while what the driver stores in it is `showsWhatIsUnder`: the base spends a
		// starred paragraph on those being different questions (ibCompositionDriver::OnGroup), and a
		// heading standing over rows this output does not print HAS children and must not offer an
		// expander. The consumers ask it to decide `isContainer`, so the name is now the question.
		bool m_expandable = false;
		std::map<ibMetaID, ibValue> m_values;   // by the attribute metaID
		// ⭐⭐ A HEADING'S OWN KEY — the value of the field its level groups by, as the OUTPUT says which column
		// that is (the dimension column of this heading's level). Asked of the output, not walked out of the
		// grouping's path by the reader: the list walked `Recorder.Counterparty` through the references itself,
		// stopped at the composite recorder, and every heading came out keyless — no caption, no expander, and
		// the page window never found its anchor, so the same headings arrived twice (2026-09-29).
		ibValue m_key;
		bool    m_keyed = false;   // …and whether the output had one to give: a record has none
		// ⭐ …AND WHAT ITS CONDITIONAL APPEARANCE DRAWS IT WITH, kept with it — the row's, and each column's under
		// the same id its value is filed under, beside m_values (only where a rule said something). Made by the walk
		// (ibCompositionLineAttr) and MOVED here; the model moves it on to its node as it is.
		// ⚠ NULL WHERE NO RULE HELD — a list with no conditional appearance carries nothing for it per row, not even
		// an empty map (Max, 2026-09-30: "no conditional appearance, nothing to count").
		std::unique_ptr<ibCompositionRowAttr> m_attr;

		Row() = default;
		Row(Row&&) = default;
		Row& operator=(Row&&) = default;
		// …AND A COPY, ITS APPEARANCE WITH IT — a list serves a folded level again to the page next to it
		// (ibValueModelCursor::m_foldedLevel), and every node built from a row takes the appearance as its own.
		Row(const Row& other)
			: m_level(other.m_level), m_indent(other.m_indent), m_expandable(other.m_expandable),
			  m_values(other.m_values), m_key(other.m_key), m_keyed(other.m_keyed),
			  m_attr(other.m_attr != nullptr ? std::make_unique<ibCompositionRowAttr>(*other.m_attr) : nullptr) {}
		Row& operator=(const Row& other) {
			if (this != &other) { Row copy(other); *this = std::move(copy); }
			return *this;
		}

		ibValue GetValue(const ibMetaID& id) const {
			const auto it = m_values.find(id);
			return it != m_values.end() ? it->second : ibValue();
		}
	};

	// Full (single-batch) read — an enum list.
	ibListFetchDriver() = default;

	// Paged read — the envelope the model built (anchor / direction / count + the hierarchy scope, if any).
	// The model fills m_hierarchy* on the request directly (RunComposerPage); there is no separate scope object.
	//
	// ⭐ …AND THE COLUMNS THE ROWS ARE FILED UNDER ARE THE READER'S, asked by name (`idOf`, the model's own
	// lookup). The statement's output columns are the statement's: on a plain read they are the attributes
	// themselves, but a grouped read goes to the server as a DECLARED nested source (`WITH q_sub0`), whose
	// columns carry numbers of their own — and a list reading every cell by its attribute's id found none of
	// them: a grouped chart of accounts showed its headings as empty lines (journal `list.group`, 2026-09-29:
	// "read under column 1067, ABSENT; carries -646 … -6").
	using ColumnIdOf = std::function<ibMetaID(const wxString&)>;
	ibListFetchDriver(const ibReadPageRequest& page, ColumnIdOf idOf)
		: m_paged(true), m_page(page), m_idOf(std::move(idOf)) {}

	// ⚠ ONE FETCH'S SINK, NEVER COPIED — its rows are taken from it (a row's attributes move on to the node, see
	// Row::m_attr). Said out loud because the class is exported: MSVC generates every member of an exported class,
	// and a generated copy would have to copy rows that only move.
	ibListFetchDriver(const ibListFetchDriver&) = delete;
	ibListFetchDriver& operator=(const ibListFetchDriver&) = delete;

	bool GetPageRequest(ibReadPageRequest& request) const override {
		if (!m_paged)
			return false;
		request = m_page;
		return true;
	}

	// THE OUTPUT STARTS, and its schema arrives with it — a list keeps the columns it is about to be
	// given rows for. (It never draws a second output: a list HAS one, and says so at the call.)
	void OnOutputBegin(const ibCompositionOutputInfo& info) override {
		m_schema = info.m_schema;
		m_rows.clear();
	}

	// ⭐ A LIST TAKES THE SECOND ANSWER — `showsWhatIsUnder`, never `hasChildren`. What it does with
	// the flag is draw an EXPANDER, and an expander may promise only what the output will actually
	// show; a heading standing over rows this output does not print must not offer to open.
	virtual void OnGroupBegin(const ibCompositionLine& line, const std::vector<ibValue>& values) override {
		Append(line.m_level, line.m_indent, line.m_showsWhatIsUnder, /*heading*/ true, values, line.m_attr);
	}

	// A RECORD OPENS NOTHING, so it offers no expander — the truthful answer, not a default.
	virtual void OnRow(const ibCompositionLine& line, const std::vector<ibValue>& values) override {
		Append(line.m_level, line.m_indent, false, /*heading*/ false, values, line.m_attr);
	}

	// (⛔ NO OnGroupEnd HERE, and that is a fact about a list rather than an omission: a list draws a
	//  heading as a LINE, and what a closing event is for is putting a total underneath — which a
	//  list has no place for. The default does nothing, which is exactly right.)

private:
	void Append(int level, int indent, bool expandable, bool heading, const std::vector<ibValue>& values,
		ibCompositionLineAttr* attr) {
		Row row;
		row.m_level = level;
		row.m_indent = indent;
		row.m_expandable = expandable;
		if (attr != nullptr) {
			row.m_attr = std::make_unique<ibCompositionRowAttr>();
			row.m_attr->m_line = std::move(attr->m_line);
		}
		for (size_t i = 0; i < m_schema.size() && i < values.size(); ++i) {
			// A heading's key — the first dimension column of its level (a column counts levels from 0, a line from 1).
			if (heading && !row.m_keyed && m_schema[i].m_role == ibQueryLowering::ibColumnRole::Dimension
			    && m_schema[i].m_level + 1 == level) {
				row.m_key   = values[i];
				row.m_keyed = true;
			}
			const ibBackendQueryColumn* col = m_schema[i].m_col;
			if (col == nullptr)
				continue;
			ibMetaID id = col->GetColumnId();
			if (m_idOf) {
				const ibMetaID own = m_idOf(m_schema[i].m_name);
				if (own != wxNOT_FOUND)
					id = own;
				// ⚠ …AND A COLUMN ONLY THE QUERY HAS STAYS IN THE QUERY. Its id is synthetic — negative, the
				// query took the minus for them and says "none" with 0 — while the reader's "none" is -1, and
				// the query's first synthetic output IS -1 (`dim0`). Filed here, a lookup that found nothing
				// read that column's value. A heading's key travels as m_key; nothing else of it is the reader's.
				else if (ibBackendQueryColumn::IsSyntheticId(id))
					continue;
			}
			row.m_values.emplace(id, values[i]);
			if (attr != nullptr)
				for (std::pair<size_t, ibCompositionAttr>& cell : attr->m_cells)   // a column's own, where it has one
					if (cell.first == i) {
						row.m_attr->m_columns.emplace(id, std::move(cell.second));
						break;
					}
		}
		m_rows.push_back(std::move(row));
	}

public:
	std::vector<Row>& Rows() { return m_rows; }
	const std::vector<Row>& Rows() const { return m_rows; }

private:
	bool              m_paged = false;
	ibReadPageRequest m_page;
	ColumnIdOf        m_idOf;   // the reader's column id by output name — empty: the output column's own

	std::vector<ibQueryLowering::OutputColumn> m_schema;
	std::vector<Row>                           m_rows;
};

#endif
