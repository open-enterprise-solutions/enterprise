#include "docViewProfile.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include <wx/sizer.h>
#include <wx/tokenzr.h>

#include "deskgui/univ/aui/auibar.h"      // the bar of the levels' checks
#include "deskgui/univ/ctrl/checkBox.h"

#include "appstudio/mainFrame/mainFrameStudio.h"
#include "appstudio/mainFrame/metaTree/treeApplication.h"   // EditModule — a row opens its line
#include "core/localization/dictionary.h"                    // _() — the columns' words
#include "protocol/protocolStudio.h"                       // ibProtocol::ibView::ibProfile

namespace {

enum {
	ColumnName,
	ColumnProcedure,
	ColumnLine,
	ColumnCount,
	ColumnSelf,
	ColumnShare,
	ColumnTotal,
	ColumnDatabase,
	ColumnMemory,
	ColumnRows,
	ColumnRequest,
	ColumnAnswer,
};

// The measurement as the server's view wrote it (ibProfileView::Fetch) — read back into the structure it was.
ibProfileData ProfileOf(const ibProtocolNode& node)
{
	namespace key = ibProtocol::ibView::ibProfile;

	// A statement as the database got it, under a query or straight under the line.
	const auto statementsOf = [](const ibProtocolNode& parent) {
		std::vector<ibQueryDebugData::ibStatement> statements;
		for (const ibProtocolNode& row : parent.FindChild(key::Statements).Children()) {
			ibQueryDebugData::ibStatement statement;
			statement.m_text  = row.GetString(key::Text);
			statement.m_plan  = row.GetString(key::Plan);
			statement.m_dbms  = row.GetString(key::Dbms);
			statement.m_count = static_cast<std::uint64_t>(row.GetInt(key::Count));
			statement.m_rows  = static_cast<std::uint64_t>(row.GetInt(key::RowCount));
			statement.m_run     = row.GetInt(key::Run);
			statement.m_fetch   = row.GetInt(key::Fetch);
			statement.m_slowest = row.GetInt(key::Slowest);
			for (const ibProtocolNode& value : row.FindChild(key::Values).Children())
				statement.m_values.push_back(value.GetString(key::Text));
			statements.push_back(statement);
		}
		return statements;
	};

	// The lines — of a call, or of none.
	const auto rowsOf = [&statementsOf](const ibProtocolNode& parent) {
		std::vector<ibProfileData::ibProfileRow> lines;
		for (const ibProtocolNode& row : parent.FindChild(key::Rows).Children()) {
			ibProfileData::ibProfileRow line;
			line.m_moduleName = row.GetString(key::Module);
			line.m_name       = row.GetString(key::Name);
			line.m_procedure  = row.GetString(key::Procedure);
			line.m_line       = static_cast<unsigned int>(row.GetInt(key::Line));
			line.m_count      = static_cast<std::uint64_t>(row.GetInt(key::Count));
			line.m_self       = row.GetInt(key::Self);
			line.m_total      = row.GetInt(key::Total);
			for (const ibProtocolNode& written : row.FindChild(key::Queries).Children()) {
				ibQueryDebugData::ibQuery query;
				query.m_source     = written.GetString(key::Source);
				query.m_count      = static_cast<std::uint64_t>(written.GetInt(key::Count));
				query.m_total      = written.GetInt(key::Total);
				query.m_database   = written.GetInt(key::Database);
				query.m_statements = statementsOf(written);
				line.m_queries.m_queries.push_back(query);
			}
			line.m_queries.m_statements = statementsOf(row);
			lines.push_back(line);
		}
		return lines;
	};

	ibProfileData profile;
	profile.m_duration = node.GetInt(key::Duration);
	for (const ibProtocolNode& written : node.FindChild(key::Calls).Children()) {
		ibProfileData::ibProfileCall call;
		call.m_method       = written.GetString(key::Method);
		call.m_count        = static_cast<std::uint64_t>(written.GetInt(key::Count));
		call.m_total        = written.GetInt(key::Total);
		call.m_requestBytes = static_cast<std::uint64_t>(written.GetInt(key::RequestBytes));
		call.m_answerBytes  = static_cast<std::uint64_t>(written.GetInt(key::AnswerBytes));
		call.m_rows         = rowsOf(written);
		profile.m_calls.push_back(call);
	}
	profile.m_rows = rowsOf(node);
	return profile;
}

wxString Milliseconds(std::int64_t microseconds)
{
	return wxString::Format(wxT("%.3f"), microseconds / 1000.0);
}

wxString Number(std::uint64_t number)
{
	return wxString::Format(wxT("%llu"), static_cast<unsigned long long>(number));
}

// A statement's text in one line of the tree: its line breaks and runs of spaces are one space. The whole text is the
// row's tooltip.
wxString OneLine(const wxString& text)
{
	wxString line;
	bool space = false;
	for (const wxUniChar c : text) {
		if (c == wxT(' ') || c == wxT('\t') || c == wxT('\r') || c == wxT('\n')) {
			space = !line.empty();
			continue;
		}
		if (space)
			line += wxT(' ');
		space = false;
		line += c;
	}
	return line;
}

// What a line's row shows over its children: the time its statements spent in the database — run and fetched, the
// rows of a cursor drawn after its query returned among them — the time its queries spent in memory, and the rows its
// statements read or changed.
struct ibQuerySums {
	std::int64_t  m_database = 0;
	std::int64_t  m_memory   = 0;
	std::uint64_t m_rows     = 0;
};

ibQuerySums SumsOf(const ibQueryDebugData& queries)
{
	ibQuerySums sums;
	const auto add = [&sums](const ibQueryDebugData::ibStatement& statement) {
		sums.m_database += statement.m_run + statement.m_fetch;
		sums.m_rows     += statement.m_rows;
	};
	for (const ibQueryDebugData::ibQuery& query : queries.m_queries) {
		sums.m_memory += std::max<std::int64_t>(query.m_total - query.m_database, 0);
		for (const ibQueryDebugData::ibStatement& statement : query.m_statements)
			add(statement);
	}
	for (const ibQueryDebugData::ibStatement& statement : queries.m_statements)
		add(statement);
	return sums;
}

// …and a call's row, over the lines it ran.
ibQuerySums SumsOf(const ibProfileData::ibProfileCall& call)
{
	ibQuerySums sums;
	for (const ibProfileData::ibProfileRow& row : call.m_rows) {
		const ibQuerySums line = SumsOf(row.m_queries);
		sums.m_database += line.m_database;
		sums.m_memory   += line.m_memory;
		sums.m_rows     += line.m_rows;
	}
	return sums;
}

// …and a query's row, over its statements: its time in the database as it measured it, the rest in memory.
ibQuerySums SumsOf(const ibQueryDebugData::ibQuery& query)
{
	ibQuerySums sums;
	sums.m_database = query.m_database;
	sums.m_memory   = std::max<std::int64_t>(query.m_total - query.m_database, 0);
	for (const ibQueryDebugData::ibStatement& statement : query.m_statements)
		sums.m_rows += statement.m_rows;
	return sums;
}

// A call's own time: what its lines did not take — the request read, the answer written, the forms' own work.
std::int64_t SelfOf(const ibProfileData::ibProfileCall& call)
{
	std::int64_t lines = 0;
	for (const ibProfileData::ibProfileRow& row : call.m_rows)
		lines += row.m_self;
	return std::max<std::int64_t>(call.m_total - lines, 0);
}

// THE SAME THING MET MORE THAN ONCE, AS ONE ROW — a level taken off lifts what was under it, and a statement sent under
// several lines, a query run under several, a line run under several calls meet there. A statement by its text: its
// runs added, its slowest run the slowest of them.
std::vector<ibQueryDebugData::ibStatement> Merged(const std::vector<ibQueryDebugData::ibStatement>& statements)
{
	std::vector<ibQueryDebugData::ibStatement> merged;
	std::map<ibString, std::size_t> at;
	for (const ibQueryDebugData::ibStatement& statement : statements) {
		const auto [found, made] = at.try_emplace(statement.m_text, merged.size());
		if (made) {
			merged.push_back(statement);
			continue;
		}
		ibQueryDebugData::ibStatement& sum = merged[found->second];
		sum.m_count += statement.m_count;
		sum.m_rows  += statement.m_rows;
		sum.m_run   += statement.m_run;
		sum.m_fetch += statement.m_fetch;
		if (sum.m_plan.IsEmpty())
			sum.m_plan = statement.m_plan;
		if (statement.m_slowest > sum.m_slowest) {
			sum.m_slowest = statement.m_slowest;
			sum.m_values  = statement.m_values;
		}
	}
	return merged;
}

// …a query by what it reads from: its runs added, its statements as one.
std::vector<ibQueryDebugData::ibQuery> Merged(const std::vector<ibQueryDebugData::ibQuery>& queries)
{
	std::vector<ibQueryDebugData::ibQuery> merged;
	std::map<ibString, std::size_t> at;
	for (const ibQueryDebugData::ibQuery& query : queries) {
		const auto [found, made] = at.try_emplace(query.m_source, merged.size());
		if (made) {
			merged.push_back(query);
			continue;
		}
		ibQueryDebugData::ibQuery& sum = merged[found->second];
		sum.m_count    += query.m_count;
		sum.m_total    += query.m_total;
		sum.m_database += query.m_database;
		sum.m_statements.insert(sum.m_statements.end(), query.m_statements.begin(), query.m_statements.end());
	}
	for (ibQueryDebugData::ibQuery& query : merged)
		query.m_statements = Merged(query.m_statements);
	return merged;
}

// …a line by its module and its number — the platform's rows all one: its sums added, what it ran as one.
std::vector<ibProfileData::ibProfileRow> Merged(const std::vector<ibProfileData::ibProfileRow>& lines)
{
	std::vector<ibProfileData::ibProfileRow> merged;
	std::map<std::pair<ibString, unsigned int>, std::size_t> at;
	for (const ibProfileData::ibProfileRow& line : lines) {
		const auto [found, made] = at.try_emplace(std::make_pair(line.m_moduleName, line.m_line), merged.size());
		if (made) {
			merged.push_back(line);
			continue;
		}
		ibProfileData::ibProfileRow& sum = merged[found->second];
		sum.m_count += line.m_count;
		sum.m_self  += line.m_self;
		sum.m_total += line.m_total;
		sum.m_queries.m_queries.insert(sum.m_queries.m_queries.end(),
			line.m_queries.m_queries.begin(), line.m_queries.m_queries.end());
		sum.m_queries.m_statements.insert(sum.m_queries.m_statements.end(),
			line.m_queries.m_statements.begin(), line.m_queries.m_statements.end());
	}
	for (ibProfileData::ibProfileRow& line : merged) {
		line.m_queries.m_queries    = Merged(line.m_queries.m_queries);
		line.m_queries.m_statements = Merged(line.m_queries.m_statements);
	}
	return merged;
}

// The rows of a level in the order asked for: `before` says which of two comes first from the smallest, `ascending`
// which way it is read.
template <typename T, typename Before>
std::vector<const T*> Sorted(const std::vector<T>& items, const Before& before, bool ascending)
{
	std::vector<const T*> sorted;
	for (const T& item : items)
		sorted.push_back(&item);
	std::stable_sort(sorted.begin(), sorted.end(),
		[&](const T* a, const T* b) { return ascending ? before(*a, *b) : before(*b, *a); });
	return sorted;
}

// What a row stands for; the tree deletes it with the row. Plans' and values' rows have none.
class ibRowData final : public wxTreeItemData {
public:
	explicit ibRowData(const wxString& key, const ibString& module = ibString(), unsigned int line = 0)
		: m_key(key), m_module(module), m_line(line) {}
	// …its path — the call, the line, the query, the statement — what a row that stood open is known by when the tree
	// is filled again: what is shown is made again, so no address holds.
	const wxString     m_key;
	const ibString     m_module;   // a line's module (its id) and its number — what its row opens
	const unsigned int m_line;
};

// The rows that stand open under `parent`, by their paths — under a closed row too, which keeps them for its opening.
void Remember(ibTreeListCtrl* tree, const wxTreeItemId& parent, std::set<wxString>& expanded)
{
	wxTreeItemIdValue cookie;
	for (wxTreeItemId item = tree->GetFirstChild(parent, cookie); item.IsOk(); item = tree->GetNextChild(parent, cookie)) {
		const ibRowData* const data = static_cast<const ibRowData*>(tree->GetItemData(item));
		if (data == nullptr)
			continue;
		if (tree->IsExpanded(item))
			expanded.insert(data->m_key);
		Remember(tree, item, expanded);
	}
}

} // namespace

// ----------------------------------------------------------------------------
// ibProfileView implementation
// ----------------------------------------------------------------------------

// What it shows is asked once, as it is made: what was measured does not change.
bool ibProfileView::OnCreate(ibFrontendDocument* docBase, long flags)
{
	const ibTabDocument* const document = dynamic_cast<const ibTabDocument*>(docBase);
	ibProtocolNode answer;
	if (document != nullptr && document->Fetch(ibProtocolNode(), answer))
		m_profile = ProfileOf(answer);
	for (const ibProfileData::ibProfileCall& call : m_profile.m_calls) {
		m_spent += SelfOf(call);
		for (const ibProfileData::ibProfileRow& row : call.m_rows)
			m_spent += row.m_self;
	}
	for (const ibProfileData::ibProfileRow& row : m_profile.m_rows)
		m_spent += row.m_self;
	m_sortColumn = ColumnSelf;

	m_tree = new ibTreeListCtrl(m_viewFrame, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HIDE_ROOT | wxTR_FULL_ROW_HIGHLIGHT | wxTR_ROW_LINES | wxTR_HAS_BUTTONS | wxTR_NO_LINES | wxTR_SINGLE);
	m_tree->AddColumn(_("Name"), 300, wxALIGN_LEFT);
	m_tree->AddColumn(_("Procedure"), 160, wxALIGN_LEFT);
	m_tree->AddColumn(_("Line"), 50, wxALIGN_RIGHT);
	m_tree->AddColumn(_("Count"), 70, wxALIGN_RIGHT);
	m_tree->AddColumn(_("Own, ms"), 90, wxALIGN_RIGHT);
	m_tree->AddColumn(wxT("%"), 50, wxALIGN_RIGHT);
	m_tree->AddColumn(_("Total, ms"), 90, wxALIGN_RIGHT);
	m_tree->AddColumn(_("Database, ms"), 100, wxALIGN_RIGHT);
	m_tree->AddColumn(_("In memory, ms"), 100, wxALIGN_RIGHT);
	m_tree->AddColumn(_("Rows"), 80, wxALIGN_RIGHT);
	m_tree->AddColumn(_("Request, bytes"), 100, wxALIGN_RIGHT);
	m_tree->AddColumn(_("Answer, bytes"), 100, wxALIGN_RIGHT);
	m_root = m_tree->AddRoot(wxEmptyString);
	m_tree->Bind(wxEVT_LIST_COL_CLICK, &ibProfileView::OnColumnClick, this);
	m_tree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibProfileView::OnItemActivated, this);

	// What the tree shows, a level a check — all of it at first; a check's id says its level.
	ibAuiToolBar* const bar = new ibAuiToolBar(m_viewFrame, wxID_ANY, wxDefaultPosition, wxDefaultSize, ibAUI_TB_HORZ_TEXT);
	const wxString levels[] = { _("Calls"), _("Lines"), _("Queries"), _("SQL"), _("Plans"), _("Values") };
	for (int level = ShowCalls; level <= ShowValues; ++level) {
		ibCheckBox* const check = new ibCheckBox(bar, wxID_HIGHEST + 1 + level, levels[level]);
		check->SetValue(true);
		check->Bind(wxEVT_CHECKBOX, &ibProfileView::OnShown, this);
		bar->AddControl(check);
	}
	bar->Realize();

	wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(bar, 0, wxEXPAND);
	sizer->Add(m_tree, 1, wxEXPAND);
	m_viewFrame->SetSizer(sizer);
	m_viewFrame->Layout();

	Fill();
	return ibFrontendView::OnCreate(docBase, flags);
}

void ibProfileView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here, the tree draws itself
}

void ibProfileView::Fill()
{
	// A row that stood open stays open: the tree is made again, so it is known by its path.
	std::set<wxString> expanded;
	Remember(m_tree, m_root, expanded);
	m_tree->DeleteChildren(m_root);

	const auto shown = [this](ibShown level) { return (m_shown & (1u << level)) != 0; };
	const auto share = [this](std::int64_t self) {
		return wxString::Format(wxT("%.1f"), m_spent > 0 ? self * 100.0 / m_spent : 0.0);
	};
	// …once what it holds is under it: a check may have taken a statement's plan away.
	const auto restore = [this, &expanded](const wxTreeItemId& item, const wxString& key) {
		if (expanded.count(key) != 0 && m_tree->HasChildren(item))
			m_tree->Expand(item);
	};

	// What a column means at each level; a level with no own time sorts by its time instead.
	const auto callBefore = [this](const ibProfileData::ibProfileCall& a, const ibProfileData::ibProfileCall& b) {
		switch (m_sortColumn) {
		case ColumnName:
		case ColumnProcedure: return a.m_method < b.m_method;
		case ColumnCount:     return a.m_count < b.m_count;
		case ColumnTotal:     return a.m_total < b.m_total;
		case ColumnDatabase:  return SumsOf(a).m_database < SumsOf(b).m_database;
		case ColumnMemory:    return SumsOf(a).m_memory < SumsOf(b).m_memory;
		case ColumnRows:      return SumsOf(a).m_rows < SumsOf(b).m_rows;
		case ColumnRequest:   return a.m_requestBytes < b.m_requestBytes;
		case ColumnAnswer:    return a.m_answerBytes < b.m_answerBytes;
		default:              return SelfOf(a) < SelfOf(b);   // its own time, and that time's share
		}
	};
	const auto lineBefore = [this](const ibProfileData::ibProfileRow& a, const ibProfileData::ibProfileRow& b) {
		switch (m_sortColumn) {
		case ColumnName:      return a.m_name < b.m_name;
		case ColumnProcedure: return a.m_procedure < b.m_procedure;
		case ColumnLine:      return a.m_line < b.m_line;
		case ColumnCount:     return a.m_count < b.m_count;
		case ColumnTotal:     return a.m_total < b.m_total;
		case ColumnDatabase:  return SumsOf(a.m_queries).m_database < SumsOf(b.m_queries).m_database;
		case ColumnMemory:    return SumsOf(a.m_queries).m_memory < SumsOf(b.m_queries).m_memory;
		case ColumnRows:      return SumsOf(a.m_queries).m_rows < SumsOf(b.m_queries).m_rows;
		default:              return a.m_self < b.m_self;   // its own time, and that time's share
		}
	};
	const auto queryBefore = [this](const ibQueryDebugData::ibQuery& a, const ibQueryDebugData::ibQuery& b) {
		switch (m_sortColumn) {
		case ColumnName:
		case ColumnProcedure: return a.m_source < b.m_source;
		case ColumnCount:     return a.m_count < b.m_count;
		case ColumnDatabase:  return SumsOf(a).m_database < SumsOf(b).m_database;
		case ColumnMemory:    return SumsOf(a).m_memory < SumsOf(b).m_memory;
		case ColumnRows:      return SumsOf(a).m_rows < SumsOf(b).m_rows;
		default:              return a.m_total < b.m_total;
		}
	};
	const auto statementBefore = [this](const ibQueryDebugData::ibStatement& a, const ibQueryDebugData::ibStatement& b) {
		switch (m_sortColumn) {
		case ColumnName:
		case ColumnProcedure: return a.m_text < b.m_text;
		case ColumnCount:     return a.m_count < b.m_count;
		case ColumnRows:      return a.m_rows < b.m_rows;
		default:              return a.m_run + a.m_fetch < b.m_run + b.m_fetch;
		}
	};

	// A statement as the database got it; under it its slowest run's values, in the order its placeholders take them,
	// with that run's time, and its plan, a line of the plan a row.
	const auto appendStatement = [&](const wxTreeItemId& parent, const ibQueryDebugData::ibStatement& statement,
		const wxString& path) {
		const wxString text = statement.m_text;
		const wxString key = path + wxT("/S") + text;
		const wxTreeItemId item = m_tree->AppendItem(parent, OneLine(text), -1, -1, new ibRowData(key));
		m_tree->SetItemText(item, ColumnCount, Number(statement.m_count));
		m_tree->SetItemText(item, ColumnTotal, Milliseconds(statement.m_run + statement.m_fetch));
		m_tree->SetItemText(item, ColumnDatabase, Milliseconds(statement.m_run + statement.m_fetch));
		m_tree->SetItemText(item, ColumnRows, Number(statement.m_rows));

		wxString tip = statement.m_dbms;
		tip << wxT("\n\n") << text;
		m_tree->SetItemToolTip(item, tip);

		if (shown(ShowValues) && !statement.m_values.empty()) {
			wxString values;
			wxString each;
			for (const ibString& value : statement.m_values) {
				values << (values.empty() ? wxString() : wxString(wxT(", "))) << value;
				each << (each.empty() ? wxString() : wxString(wxT("\n"))) << value;
			}
			const wxTreeItemId row = m_tree->AppendItem(item, wxString::Format(_("Values: %s"), OneLine(values)));
			m_tree->SetItemText(row, ColumnTotal, Milliseconds(statement.m_slowest));
			m_tree->SetItemToolTip(row, each);
		}
		if (shown(ShowPlans)) {
			const wxString plan = statement.m_plan;
			for (const wxString& step : wxStringTokenize(plan, wxT("\r\n"), wxTOKEN_STRTOK)) {
				const wxTreeItemId row = m_tree->AppendItem(item, step);
				m_tree->SetItemToolTip(row, plan);
			}
		}
		restore(item, key);
	};
	const auto appendStatements = [&](const wxTreeItemId& parent,
		const std::vector<ibQueryDebugData::ibStatement>& statements, const wxString& path) {
		for (const ibQueryDebugData::ibStatement* statement : Sorted(statements, statementBefore, m_ascending))
			appendStatement(parent, *statement, path);
	};

	// A query: how often it ran, its time, the database's and memory's of it, the rows its statements read or changed;
	// under it the statements it sent.
	const auto appendQuery = [&](const wxTreeItemId& parent, const ibQueryDebugData::ibQuery& query, const wxString& path) {
		const wxString source = query.m_source;
		const wxString key = path + wxT("/Q") + source;
		const ibQuerySums sums = SumsOf(query);
		const wxTreeItemId item = m_tree->AppendItem(parent, source, -1, -1, new ibRowData(key));
		m_tree->SetItemText(item, ColumnCount, Number(query.m_count));
		m_tree->SetItemText(item, ColumnTotal, Milliseconds(query.m_total));
		m_tree->SetItemText(item, ColumnDatabase, Milliseconds(sums.m_database));
		m_tree->SetItemText(item, ColumnMemory, Milliseconds(sums.m_memory));
		m_tree->SetItemText(item, ColumnRows, Number(sums.m_rows));
		if (shown(ShowStatements))
			appendStatements(item, query.m_statements, key);
		restore(item, key);
	};

	// What lines ran: the queries, then the statements sent with no query. With the queries off their statements join
	// those, the same statement of two queries one row.
	const auto appendQueries = [&](const wxTreeItemId& parent, const ibQueryDebugData& queries, const wxString& path) {
		if (shown(ShowQueries)) {
			for (const ibQueryDebugData::ibQuery* query : Sorted(queries.m_queries, queryBefore, m_ascending))
				appendQuery(parent, *query, path);
			if (shown(ShowStatements))
				appendStatements(parent, queries.m_statements, path);
			return;
		}
		if (!shown(ShowStatements))
			return;
		std::vector<ibQueryDebugData::ibStatement> statements = queries.m_statements;
		for (const ibQueryDebugData::ibQuery& query : queries.m_queries)
			statements.insert(statements.end(), query.m_statements.begin(), query.m_statements.end());
		appendStatements(parent, Merged(statements), path);
	};

	// A line, with what it ran under it.
	const auto appendLine = [&](const wxTreeItemId& parent, const ibProfileData::ibProfileRow& row, const wxString& path) {
		wxString name = row.m_name;
		if (name.empty())
			name = _("Platform");
		const wxString module = row.m_moduleName;
		const wxString key = path + wxString::Format(wxT("/L%s:%u"), module, row.m_line);

		const ibQuerySums sums = SumsOf(row.m_queries);
		const wxTreeItemId line = m_tree->AppendItem(parent, name, -1, -1, new ibRowData(key, row.m_moduleName, row.m_line));
		m_tree->SetItemText(line, ColumnProcedure, row.m_procedure);
		if (row.m_line > 0)
			m_tree->SetItemText(line, ColumnLine, wxString::Format(wxT("%u"), row.m_line));
		m_tree->SetItemText(line, ColumnCount, Number(row.m_count));
		m_tree->SetItemText(line, ColumnSelf, Milliseconds(row.m_self));
		m_tree->SetItemText(line, ColumnShare, share(row.m_self));
		m_tree->SetItemText(line, ColumnTotal, Milliseconds(row.m_total));
		if (!row.m_queries.IsEmpty()) {
			m_tree->SetItemText(line, ColumnDatabase, Milliseconds(sums.m_database));
			m_tree->SetItemText(line, ColumnMemory, Milliseconds(sums.m_memory));
			m_tree->SetItemText(line, ColumnRows, Number(sums.m_rows));
		}
		appendQueries(line, row.m_queries, key);
		restore(line, key);
	};

	// Lines — or, with their level off, what they ran, the same query or statement of two lines one row.
	const auto appendLines = [&](const wxTreeItemId& parent, const std::vector<ibProfileData::ibProfileRow>& lines,
		const wxString& path) {
		if (shown(ShowLines)) {
			for (const ibProfileData::ibProfileRow* row : Sorted(lines, lineBefore, m_ascending))
				appendLine(parent, *row, path);
			return;
		}
		ibQueryDebugData queries;
		for (const ibProfileData::ibProfileRow& row : lines) {
			queries.m_queries.insert(queries.m_queries.end(),
				row.m_queries.m_queries.begin(), row.m_queries.m_queries.end());
			queries.m_statements.insert(queries.m_statements.end(),
				row.m_queries.m_statements.begin(), row.m_queries.m_statements.end());
		}
		queries.m_queries    = Merged(queries.m_queries);
		queries.m_statements = Merged(queries.m_statements);
		appendQueries(parent, queries, path);
	};

	// With the calls off, their lines and the lines of no call are one list, a line run under two calls one row.
	if (!shown(ShowCalls)) {
		std::vector<ibProfileData::ibProfileRow> lines = m_profile.m_rows;
		for (const ibProfileData::ibProfileCall& call : m_profile.m_calls)
			lines.insert(lines.end(), call.m_rows.begin(), call.m_rows.end());
		appendLines(m_root, Merged(lines), wxString());
		return;
	}

	// The calls first, each with what it ran under it; a name column sorts them by the method.
	for (const ibProfileData::ibProfileCall* call : Sorted(m_profile.m_calls, callBefore, m_ascending)) {
		const std::int64_t self = SelfOf(*call);
		const wxString method = call->m_method;
		const wxString key = wxT("C") + method;
		const wxTreeItemId item = m_tree->AppendItem(m_root, method, -1, -1, new ibRowData(key));
		m_tree->SetItemText(item, ColumnCount, Number(call->m_count));
		m_tree->SetItemText(item, ColumnSelf, Milliseconds(self));
		m_tree->SetItemText(item, ColumnShare, share(self));
		m_tree->SetItemText(item, ColumnTotal, Milliseconds(call->m_total));
		if (std::any_of(call->m_rows.begin(), call->m_rows.end(),
				[](const ibProfileData::ibProfileRow& row) { return !row.m_queries.IsEmpty(); })) {
			const ibQuerySums sums = SumsOf(*call);
			m_tree->SetItemText(item, ColumnDatabase, Milliseconds(sums.m_database));
			m_tree->SetItemText(item, ColumnMemory, Milliseconds(sums.m_memory));
			m_tree->SetItemText(item, ColumnRows, Number(sums.m_rows));
		}
		m_tree->SetItemText(item, ColumnRequest, Number(call->m_requestBytes));
		m_tree->SetItemText(item, ColumnAnswer, Number(call->m_answerBytes));

		appendLines(item, call->m_rows, key);
		restore(item, key);
	}

	// …then the lines of no call.
	appendLines(m_root, m_profile.m_rows, wxString());
}

// The same column again turns the order round; another one sorts by it — a name from A, a number from the largest.
void ibProfileView::OnColumnClick(wxListEvent& event)
{
	const int column = event.GetColumn();
	if (column < 0)
		return;

	if (column == m_sortColumn)
		m_ascending = !m_ascending;
	else {
		m_sortColumn = column;
		m_ascending  = column == ColumnName || column == ColumnProcedure;
	}
	Fill();
}

// A line's row — at the top or under its call — opens its module at that line. What has nothing to open — a call, a
// query, a statement, code sent to run (code_run), the platform's own work — is left to the tree, which opens or closes
// the row.
void ibProfileView::OnItemActivated(wxTreeEvent& event)
{
	const wxTreeItemId item = event.GetItem();
	const ibRowData* const data = item.IsOk() ? static_cast<const ibRowData*>(m_tree->GetItemData(item)) : nullptr;
	if (data == nullptr || data->m_module.IsEmpty()) {
		event.Skip();
		return;
	}

	const ibGuid module(data->m_module);
	if (!module.isValid() || mainFrame == nullptr || mainFrame->GetMetaWindow() == nullptr) {
		event.Skip();
		return;
	}
	mainFrame->GetMetaWindow()->EditModule(wxString(), module, static_cast<int>(data->m_line), false);
}

// A check turned: its level drawn or not, the tree made again. A plan and the values are a statement's: with the SQL
// off they have nothing to stand under, and their checks grey to say so.
void ibProfileView::OnShown(wxCommandEvent& event)
{
	const int level = event.GetId() - (wxID_HIGHEST + 1);
	if (event.IsChecked())
		m_shown |= 1u << level;
	else
		m_shown &= ~(1u << level);

	if (level == ShowStatements) {
		const wxWindow* const bar = static_cast<const wxWindow*>(event.GetEventObject())->GetParent();
		for (const int detail : { ShowPlans, ShowValues }) {
			if (wxWindow* const check = bar->FindWindow(wxID_HIGHEST + 1 + detail))
				check->Enable(event.IsChecked());
		}
	}
	Fill();
}

// ----------------------------------------------------------------------------
// ibProfileDocument implementation
// ----------------------------------------------------------------------------

ibFrontendView* ibProfileDocument::DoCreateView()
{
	return new ibProfileView();
}

TAB_DOCUMENT_REGISTER(ibProfileDocument, ibProtocol::ibView::ibKind::Profile)
