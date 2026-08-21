////////////////////////////////////////////////////////////////////////////
//	The SELECTION-LINKS grid — the model over the PACKAGE's own links.
////////////////////////////////////////////////////////////////////////////

#include "queryConstructorInternal.h"
#include "querySelectionLinkModel.h"

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
	m_package->m_links.push_back(ibQueryPackageLink());
	Reset(static_cast<unsigned int>(m_package->m_links.size()));
	if (m_onChanged)
		m_onChanged();
}

void ibQuerySelectionLinkModel::RemoveLink(unsigned int row)
{
	if (m_package == nullptr || row >= m_package->m_links.size())
		return;
	m_package->m_links.erase(m_package->m_links.begin() + row);
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

	if (m_onChanged)
		m_onChanged();
	return true;
}
