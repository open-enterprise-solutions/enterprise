////////////////////////////////////////////////////////////////////////////
//	Description : The join diagram — a second view over m_joins (queryJoinDiagram.h)
////////////////////////////////////////////////////////////////////////////

#include "queryJoinDiagram.h"

#include "backend/query/queryParser.h"
#include "backend/query/queryRender.h"
#include "backend/backend_exception.h"

#include <wx/dcbuffer.h>
#include <wx/settings.h>
#include <wx/msgdlg.h>

// NAMED, NOT INHERITED — std::max / std::min in the layout pass. MSVC hands <algorithm> over
// transitively through the wx headers above; GCC and Clang do not. (docs/portability.md)
#include <algorithm>

namespace {

// Layout constants. Deliberately fixed rather than measured per box: a diagram whose boxes change
// width as fields are added is a diagram whose lines move when nothing about the query moved.
constexpr int kBoxWidth   = 170;
constexpr int kRowHeight  = 18;
constexpr int kHeaderRow  = 22;
constexpr int kGapX       = 60;
constexpr int kGapY       = 24;
constexpr int kMargin     = 12;
constexpr size_t kMaxRows = 10;   // a box lists at most this many fields; the rest are summarised

wxColour Blend(const wxColour& base, int lightness)
{
	return base.ChangeLightness(lightness);
}

} // namespace

ibQueryJoinDiagram::ibQueryJoinDiagram(wxWindow* parent)
	: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);   // wxAutoBufferedPaintDC — no flicker while dragging

	Bind(wxEVT_PAINT,        &ibQueryJoinDiagram::OnPaint,       this);
	Bind(wxEVT_LEFT_DOWN,    &ibQueryJoinDiagram::OnLeftDown,    this);
	Bind(wxEVT_LEFT_UP,      &ibQueryJoinDiagram::OnLeftUp,      this);
	Bind(wxEVT_MOTION,       &ibQueryJoinDiagram::OnMotion,      this);
	Bind(wxEVT_LEFT_DCLICK,  &ibQueryJoinDiagram::OnDoubleClick, this);
	Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { LayoutBoxes(); Refresh(); e.Skip(); });
}

void ibQueryJoinDiagram::SetContent(ibQuerySelect* select, std::vector<Table> tables)
{
	m_select = select;
	m_tables = std::move(tables);
	m_dragging = false;
	LayoutBoxes();
	Refresh();
}

// ---------------------------------------------------------------------------
//  Layout
// ---------------------------------------------------------------------------

void ibQueryJoinDiagram::LayoutBoxes()
{
	m_boxes.clear();
	m_boxes.reserve(m_tables.size());

	const int available = std::max(GetClientSize().x, kBoxWidth + 2 * kMargin);
	const int perRow = std::max(1, (available - kMargin) / (kBoxWidth + kGapX));

	int x = kMargin, y = kMargin, rowHeight = 0, inRow = 0;
	for (const Table& table : m_tables) {
		const size_t rows = std::min(table.m_fields.size(), kMaxRows);
		const int height = kHeaderRow + static_cast<int>(rows) * kRowHeight + 6;

		if (inRow == perRow) {                       // wrap
			x = kMargin;
			y += rowHeight + kGapY;
			rowHeight = 0;
			inRow = 0;
		}
		m_boxes.push_back(wxRect(x, y, kBoxWidth, height));
		rowHeight = std::max(rowHeight, height);
		x += kBoxWidth + kGapX;
		++inRow;
	}
}

wxRect ibQueryJoinDiagram::BoxRect(size_t table) const
{
	return table < m_boxes.size() ? m_boxes[table] : wxRect();
}

wxRect ibQueryJoinDiagram::FieldRect(size_t table, size_t field) const
{
	if (table >= m_boxes.size() || field >= std::min(m_tables[table].m_fields.size(), kMaxRows))
		return wxRect();
	const wxRect box = m_boxes[table];
	return wxRect(box.x + 1, box.y + kHeaderRow + static_cast<int>(field) * kRowHeight,
		box.width - 2, kRowHeight);
}

bool ibQueryJoinDiagram::HitField(const wxPoint& at, size_t& outTable, size_t& outField) const
{
	for (size_t t = 0; t < m_boxes.size(); ++t) {
		if (!m_boxes[t].Contains(at))
			continue;
		const size_t rows = std::min(m_tables[t].m_fields.size(), kMaxRows);
		for (size_t f = 0; f < rows; ++f) {
			if (FieldRect(t, f).Contains(at)) {
				outTable = t;
				outField = f;
				return true;
			}
		}
		return false;   // inside the box but on its header — not a field
	}
	return false;
}

wxPoint ibQueryJoinDiagram::AnchorFor(size_t table, size_t field) const
{
	const wxRect row = FieldRect(table, field);
	if (row.IsEmpty())
		return BoxRect(table).GetTopLeft();
	return wxPoint(row.x + row.width / 2, row.y + row.height / 2);
}

size_t ibQueryJoinDiagram::HitJoin(const wxPoint& at) const
{
	if (m_select == nullptr)
		return static_cast<size_t>(-1);

	// A line runs between the CENTRES of two boxes; near it means within a few pixels of the
	// segment. Good enough for a click, and it needs no stored geometry per line.
	for (size_t j = 0; j < m_select->m_joins.size(); ++j) {
		const size_t right = j + 1;               // joins[j] is source index j+1
		if (right >= m_boxes.size())
			continue;
		const wxPoint a = wxPoint(m_boxes[0].x + m_boxes[0].width / 2, m_boxes[0].y + m_boxes[0].height / 2);
		const wxPoint b = wxPoint(m_boxes[right].x + m_boxes[right].width / 2, m_boxes[right].y + m_boxes[right].height / 2);

		const double dx = b.x - a.x, dy = b.y - a.y;
		const double len2 = dx * dx + dy * dy;
		if (len2 < 1.0)
			continue;
		double t = ((at.x - a.x) * dx + (at.y - a.y) * dy) / len2;
		t = std::max(0.0, std::min(1.0, t));
		const double px = a.x + t * dx, py = a.y + t * dy;
		const double distance = std::sqrt((at.x - px) * (at.x - px) + (at.y - py) * (at.y - py));
		if (distance <= 5.0)
			return j;
	}
	return static_cast<size_t>(-1);
}

// ---------------------------------------------------------------------------
//  Paint
// ---------------------------------------------------------------------------

void ibQueryJoinDiagram::OnPaint(wxPaintEvent&)
{
	wxAutoBufferedPaintDC dc(this);

	const wxColour background = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	const wxColour text       = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
	const wxColour faint      = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
	const wxColour highlight  = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
	const bool dark = (background.Red() + background.Green() + background.Blue()) < 3 * 128;

	dc.SetBackground(wxBrush(background));
	dc.Clear();

	if (m_tables.empty()) {
		dc.SetTextForeground(faint);
		// ASCII ONLY in this literal (UTF-8 without BOM + MSVC's system codepage = mojibake).
		dc.DrawText(_("Add a table on the \"Tables and fields\" tab: the joins appear here as lines."),
			kMargin, kMargin);
		return;
	}

	// THE LINES FIRST, so a box drawn over one hides its end rather than the line hiding the box.
	if (m_select != nullptr) {
		for (size_t j = 0; j < m_select->m_joins.size(); ++j) {
			const size_t right = j + 1;
			if (right >= m_boxes.size())
				continue;
			const wxPoint a(m_boxes[0].x + m_boxes[0].width / 2, m_boxes[0].y + m_boxes[0].height / 2);
			const wxPoint b(m_boxes[right].x + m_boxes[right].width / 2, m_boxes[right].y + m_boxes[right].height / 2);

			// A join WITHOUT a condition follows the reference between the tables — drawn dashed,
			// because "joined by reference" and "joined on this predicate" are different statements
			// and the picture should not make them look the same.
			const bool byReference = !m_select->m_joins[j].m_on;
			dc.SetPen(wxPen(byReference ? faint : highlight, 2,
				byReference ? wxPENSTYLE_SHORT_DASH : wxPENSTYLE_SOLID));
			dc.DrawLine(a, b);
		}
	}

	// The drag in flight — from the field it started on to the pointer.
	if (m_dragging) {
		dc.SetPen(wxPen(highlight, 2, wxPENSTYLE_DOT));
		dc.DrawLine(AnchorFor(m_dragTable, m_dragField), m_dragPoint);
	}

	for (size_t t = 0; t < m_boxes.size(); ++t) {
		const wxRect box = m_boxes[t];

		dc.SetBrush(wxBrush(Blend(background, dark ? 115 : 96)));
		dc.SetPen(wxPen(faint));
		dc.DrawRoundedRectangle(box, 4);

		// Header — the source as the query names it.
		dc.SetBrush(wxBrush(Blend(background, dark ? 130 : 88)));
		dc.DrawRoundedRectangle(wxRect(box.x, box.y, box.width, kHeaderRow), 4);
		dc.SetTextForeground(text);
		{
			// ⚠ ITS OWN SCOPE. A wxDCClipper lives until its block ends, and this one used to share
			// the box loop's body with the field rows below — so every field was clipped to the
			// header's rectangle and the boxes drew EMPTY. The fields were there all along.
			wxDCClipper headerClip(dc, wxRect(box.x + 4, box.y + 2, box.width - 8, kHeaderRow - 4));
			dc.DrawText(m_tables[t].m_title, box.x + 5, box.y + 4);
		}

		const size_t rows = std::min(m_tables[t].m_fields.size(), kMaxRows);
		for (size_t f = 0; f < rows; ++f) {
			const wxRect row = FieldRect(t, f);
			const bool active = m_dragging && m_dragTable == t && m_dragField == f;
			dc.SetTextForeground(active ? highlight : text);
			wxDCClipper rowClip(dc, wxRect(row.x + 4, row.y, row.width - 8, row.height));
			dc.DrawText(m_tables[t].m_fields[f], row.x + 6, row.y + 2);
		}

		// SAY WHAT IS NOT SHOWN. A box silently listing ten of forty fields would make the diagram
		// look like the whole truth about the table.
		if (m_tables[t].m_fields.size() > rows) {
			dc.SetTextForeground(faint);
			// ASCII ONLY in this file's literals too: UTF-8 without a BOM, read in the system
			// codepage. An ellipsis character here painted as mojibake inside the box.
			dc.DrawText(wxString::Format(_("... %u more"),
				static_cast<unsigned int>(m_tables[t].m_fields.size() - rows)),
				box.x + 6, box.y + kHeaderRow + static_cast<int>(rows) * kRowHeight - kRowHeight + 2);
		}
	}
}

// ---------------------------------------------------------------------------
//  The gesture
// ---------------------------------------------------------------------------

void ibQueryJoinDiagram::OnLeftDown(wxMouseEvent& event)
{
	event.Skip();
	if (m_readOnly)
		return;

	size_t table = 0, field = 0;
	if (!HitField(event.GetPosition(), table, field))
		return;

	m_dragging  = true;
	m_dragTable = table;
	m_dragField = field;
	m_dragPoint = event.GetPosition();
	CaptureMouse();
	Refresh();
}

void ibQueryJoinDiagram::OnMotion(wxMouseEvent& event)
{
	event.Skip();
	if (!m_dragging)
		return;
	m_dragPoint = event.GetPosition();
	Refresh();
}

void ibQueryJoinDiagram::OnLeftUp(wxMouseEvent& event)
{
	event.Skip();
	if (!m_dragging)
		return;

	m_dragging = false;
	if (HasCapture())
		ReleaseMouse();

	size_t table = 0, field = 0;
	const bool onField = HitField(event.GetPosition(), table, field);
	Refresh();

	// Dropped on nothing, or back on the same table — no join to make. A table cannot be joined to
	// itself here: that is a self-join, which needs an alias the diagram has no way to ask for.
	if (!onField || table == m_dragTable)
		return;

	ConnectFields(m_dragTable, m_dragField, table, field);
}

void ibQueryJoinDiagram::OnDoubleClick(wxMouseEvent& event)
{
	event.Skip();
	const size_t join = HitJoin(event.GetPosition());
	if (join != static_cast<size_t>(-1) && m_onEditJoin)
		m_onEditJoin(join);
}

void ibQueryJoinDiagram::ConnectFields(size_t fromTable, size_t fromField,
                                       size_t toTable, size_t toField)
{
	if (m_select == nullptr)
		return;
	if (fromTable >= m_tables.size() || toTable >= m_tables.size())
		return;

	// THE JOIN BELONGS TO THE LATER TABLE. A join attaches a source to the ones already in the
	// query, so the predicate is written on whichever of the two came second — dragging left or
	// right must produce the same query, and this is what makes that true.
	const size_t later = std::max(fromTable, toTable);
	if (later == 0 || later - 1 >= m_select->m_joins.size())
		return;

	const wxString left  = m_tables[fromTable].m_title + wxT(".") + m_tables[fromTable].m_fields[fromField];
	const wxString right = m_tables[toTable].m_title   + wxT(".") + m_tables[toTable].m_fields[toField];

	ibQueryAstJoin& join = m_select->m_joins[later - 1];

	// COMPOSED AS TEXT AND PARSED, like every other condition the constructor makes. An existing
	// predicate is EXTENDED rather than replaced: dragging a second pair of fields onto a join that
	// already has one means "and this too", which is what a composite key looks like.
	wxString text = left + wxT(" = ") + right;
	if (join.m_on)
		text = wxT("(") + ibRenderQueryExpr(*join.m_on) + wxT(") AND (") + text + wxT(")");

	try {
		ibQueryParser parser;
		join.m_on = parser.ParseExpression(text);
	}
	catch (const ibBackendException& e) {
		wxMessageBox(e.GetErrorDescription(), _("Link"), wxOK | wxICON_ERROR, this);
		return;
	}

	if (m_onChanged)
		m_onChanged();
}
