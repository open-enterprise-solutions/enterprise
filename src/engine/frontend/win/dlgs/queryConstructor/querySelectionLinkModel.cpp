////////////////////////////////////////////////////////////////////////////
//	The SELECTION-LINKS grid — the model over the PACKAGE's own links.
////////////////////////////////////////////////////////////////////////////

#include "queryConstructorInternal.h"
#include "querySelectionLinkModel.h"

std::vector<int> ibQueryLinkOwners(const ibQueryPackage& package)
{
	std::vector<int> owners(package.m_links.size(), -1);
	for (size_t s = 0; s < package.m_statements.size(); ++s) {
		const int first = package.m_statements[s].m_linkIndex;
		if (first < 0 || static_cast<size_t>(first) >= package.m_links.size())
			continue;
		const wxString& head = package.m_links[static_cast<size_t>(first)].m_left;
		if (head.IsEmpty())
			continue;   // a section with no head is not one — RenderLinkSection writes nothing for it
		for (size_t i = static_cast<size_t>(first);
		     i < package.m_links.size() && package.m_links[i].m_left.IsSameAs(head, false); ++i)
			if (owners[i] < 0)
				owners[i] = static_cast<int>(s);
	}
	return owners;
}

void ibQueryResectionLinks(ibQueryPackage& package, std::vector<int> owners)
{
	std::vector<ibQueryPackageLink>& links = package.m_links;
	owners.resize(links.size(), -1);

	// A relation complete enough to be written — the renderer skips any other (RenderLinkSection).
	const auto written = [](const ibQueryPackageLink& link) {
		return !link.m_left.IsEmpty() && !link.m_right.IsEmpty() && link.m_on != nullptr;
	};

	// 1. The head each statement stands for is its first link's; a link with another head has left.
	std::vector<wxString> headOf(package.m_statements.size());
	for (size_t i = 0; i < links.size(); ++i) {
		const int s = owners[i];
		if (s < 0 || static_cast<size_t>(s) >= headOf.size() || links[i].m_left.IsEmpty()) {
			owners[i] = -1;
			continue;
		}
		if (headOf[static_cast<size_t>(s)].IsEmpty())
			headOf[static_cast<size_t>(s)] = links[i].m_left;
		else if (!links[i].m_left.IsSameAs(headOf[static_cast<size_t>(s)], false))
			owners[i] = -1;
	}

	// 2. A link without a statement joins the first one standing for its head…
	for (size_t i = 0; i < links.size(); ++i) {
		if (owners[i] >= 0 || links[i].m_left.IsEmpty())
			continue;
		for (size_t s = 0; s < headOf.size(); ++s)
			if (!headOf[s].IsEmpty() && headOf[s].IsSameAs(links[i].m_left, false)) {
				owners[i] = static_cast<int>(s);
				break;
			}
	}

	// 3. …or opens one at the END of the package, where it runs after every selection it relates — once
	// it has a relation that can be written. Until then it waits as a row, unwritten.
	for (size_t i = 0; i < links.size(); ++i) {
		if (owners[i] >= 0 || !written(links[i]))
			continue;
		ibQueryAstStatement statement;
		statement.m_linkIndex = 0;   // a LINK statement; where its section starts is settled below
		package.m_statements.push_back(std::move(statement));
		headOf.push_back(links[i].m_left);
		const int s = static_cast<int>(package.m_statements.size()) - 1;
		for (size_t j = i; j < links.size(); ++j)
			if (owners[j] < 0 && links[j].m_left.IsSameAs(links[i].m_left, false))
				owners[j] = s;
	}

	// 4. Each statement's relations together, in the order they were written, and the rows still being
	// filled after all of them. A LINK statement with no relation left stands for nothing and goes.
	std::vector<ibQueryPackageLink>  rebuilt;
	std::vector<ibQueryAstStatement> kept;
	rebuilt.reserve(links.size());
	kept.reserve(package.m_statements.size());
	for (size_t s = 0; s < package.m_statements.size(); ++s) {
		ibQueryAstStatement& statement = package.m_statements[s];
		if (!statement.IsLink()) {
			kept.push_back(std::move(statement));
			continue;
		}
		const size_t start = rebuilt.size();
		for (size_t i = 0; i < links.size(); ++i)
			if (owners[i] == static_cast<int>(s))
				rebuilt.push_back(links[i]);
		if (rebuilt.size() == start)
			continue;
		statement.m_linkIndex = static_cast<int>(start);
		kept.push_back(std::move(statement));
	}
	for (size_t i = 0; i < links.size(); ++i)
		if (owners[i] < 0)
			rebuilt.push_back(links[i]);
	links.swap(rebuilt);
	package.m_statements.swap(kept);
}

void ibQuerySelectionLinkModel::SetContent(ibQueryPackage* package)
{
	m_package = package;
	Reset(m_package != nullptr ? static_cast<unsigned int>(m_package->m_links.size()) : 0u);
}

ibQueryPackageLink* ibQuerySelectionLinkModel::LinkAt(unsigned int row) const
{
	if (m_package == nullptr || row >= m_package->m_links.size())
		return nullptr;
	return &m_package->m_links[row];
}

// A ROW IS A LINK, and adding one adds NOTHING ELSE — no source in anybody's FROM, no statement, no
// table. The two cells are what say which selections it relates.
void ibQuerySelectionLinkModel::AddLink()
{
	if (m_package == nullptr)
		return;
	std::vector<int> owners = ibQueryLinkOwners(*m_package);
	m_package->m_links.push_back(ibQueryPackageLink());
	owners.push_back(-1);
	ibQueryResectionLinks(*m_package, std::move(owners));
	Reset(static_cast<unsigned int>(m_package->m_links.size()));
	if (m_onChanged)
		m_onChanged();
}

void ibQuerySelectionLinkModel::RemoveLink(unsigned int row)
{
	if (m_package == nullptr || row >= m_package->m_links.size())
		return;
	std::vector<int> owners = ibQueryLinkOwners(*m_package);
	m_package->m_links.erase(m_package->m_links.begin() + row);
	owners.erase(owners.begin() + row);
	ibQueryResectionLinks(*m_package, std::move(owners));
	Reset(static_cast<unsigned int>(m_package->m_links.size()));
	if (m_onChanged)
		m_onChanged();
}

void ibQuerySelectionLinkModel::GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const
{
	const ibQueryPackageLink* link = LinkAt(row);
	if (link == nullptr)
		return;   // the view can paint a row the list no longer has (a Reset lands after the paint)

	switch (col) {
	case kLinkColLeftTable:  variant = link->m_left;  break;
	case kLinkColRightTable: variant = link->m_right; break;
	case kLinkColAllLeft:    variant = ibJoinAllFromLeft(link->m_kind);  break;
	case kLinkColAllRight:   variant = ibJoinAllFromRight(link->m_kind); break;
	case kLinkColCondition:
		// An empty condition shows as EMPTY: it is a link declared and not written yet, which is an
		// ordinary state — the row exists so the two selections can be picked in it.
		variant = link->m_on ? ibRenderQueryExpr(*link->m_on) : wxString();
		break;
	// ⚠ NO "ARBITRARY" CELL — the grid has no such column here (see BuildSelectionLinksPage). Every
	// link between two selections is written by hand, so the flag would be true on every row and
	// answer to nothing.
	default:
		break;
	}
}

bool ibQuerySelectionLinkModel::SetValueByRow(const wxVariant& variant, unsigned row, unsigned col)
{
	ibQueryPackageLink* link = LinkAt(row);
	if (link == nullptr)
		return false;
	// Whose section the row was in BEFORE the cell changed it — a new head moves it, a condition
	// written at last may give it a statement (ibQueryResectionLinks, after the switch).
	std::vector<int> owners = ibQueryLinkOwners(*m_package);

	switch (col) {
	// THE TWO SELECTIONS — names, and nothing but names. Picking one does not put anything into a
	// query: which statement produced that name is the package's business, and this row only says
	// that these two results are related.
	case kLinkColLeftTable:  link->m_left  = variant.GetString(); break;
	case kLinkColRightTable: link->m_right = variant.GetString(); break;
	case kLinkColAllLeft:    link->m_kind = ibJoinKindOf(variant.GetBool(), ibJoinAllFromRight(link->m_kind)); break;
	case kLinkColAllRight:   link->m_kind = ibJoinKindOf(ibJoinAllFromLeft(link->m_kind), variant.GetBool());  break;
	case kLinkColCondition: {
		// TYPED WHERE IT STANDS, and read by the ENGINE — the same door the Links tab's condition
		// goes through. Emptying the cell UNSETS the condition and leaves the row: a link the author
		// has opened and not written is a row, not a mistake.
		wxString text = variant.GetString();
		text.Trim(true).Trim(false);
		if (text.IsEmpty()) {
			link->m_on = nullptr;
			break;
		}
		try {
			ibQueryParser parser;
			link->m_on = parser.ParseExpression(text);
		}
		catch (const ibBackendException& error) {
			if (m_onError)
				m_onError(error.GetErrorDescription());
			return false;
		}
		break;
	}
	default:
		return false;
	}

	ibQueryResectionLinks(*m_package, std::move(owners));
	Reset(static_cast<unsigned int>(m_package->m_links.size()));   // a row may have moved to its head's section
	if (m_onChanged)
		m_onChanged();
	return true;
}
