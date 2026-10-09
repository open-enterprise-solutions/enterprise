////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the rows of a table of a view
////////////////////////////////////////////////////////////////////////////

#include "tableModel.h"

#include <algorithm>


namespace {

// The most a portion asks for — the server's own limit (frmserver tableBox: s_maxFetchCount); a read of "all"
// (a tree's children, count 0) asks for as many.
constexpr int kMaxFetchCount = 500;

// A picture's id as the wire writes it — a decimal string: a u64 is past what a JSON number keeps.
ibPictureID PictureIdOf(const wxString& text)
{
	unsigned long long id = 0;
	return text.ToULongLong(&id) ? static_cast<ibPictureID>(id) : 0;
}

} // namespace

//***********************************************************************************
//*                                     Row                                         *
//***********************************************************************************

bool ibViewTableRow::IsEqualTo(const ibDataViewObject& other) const
{
	// The data view asks it of any object it holds — a row of this table, or its own marker.
	const ibViewTableRow* const row = dynamic_cast<const ibViewTableRow*>(&other);
	return row != nullptr && row->m_handle == m_handle;
}

bool ibViewTableRow::GetGroupCaption(wxString& caption) const
{
	if (m_group.IsEmpty())
		return false;
	caption = m_group;
	return true;
}

const ibViewCell* ibViewTableRow::FindCell(long long column) const
{
	const auto found = m_cells.find(column);
	return found != m_cells.end() ? &found->second : nullptr;
}

void ibViewTableRow::Read(const ibProtocolNode& row)
{
	m_container = row.GetBool(ibProtocolName::Container);
	m_group = row.GetString(ibProtocolName::Group);
	m_picture = PictureIdOf(row.GetString(ibProtocolName::Picture));

	for (const ibProtocolNode& node : row.Children()) {
		ibViewCell& cell = m_cells[node.GetId()];
		cell.text = node.GetString(ibProtocolName::Text);
		if (node.Has(ibProtocolName::Checked))
			cell.checked = node.GetBool(ibProtocolName::Checked);
		cell.number = node.GetBool(ibProtocolName::Number);
		cell.editable = node.Has(ibProtocolName::Value);

		// Its look — what of it the server set (wxColour is a plain value here: this row is no one's yet).
		const wxString colour = node.GetString(ibProtocolName::TextColour);
		if (!colour.IsEmpty())
			cell.look.SetColour(wxColour(colour));
		const wxString background = node.GetString(ibProtocolName::BackgroundColour);
		if (!background.IsEmpty())
			cell.look.SetBackgroundColour(wxColour(background));
		cell.look.SetBold(node.GetBool(ibProtocolName::Bold));
		cell.look.SetItalic(node.GetBool(ibProtocolName::Italic));
		cell.look.SetStrikethrough(node.GetBool(ibProtocolName::Strikethrough));
		cell.look.SetUnderlined(node.GetBool(ibProtocolName::Underlined));
		cell.look.SetPointSize(static_cast<int>(node.GetInt(ibProtocolName::Size)));
		cell.look.SetFaceName(node.GetString(ibProtocolName::Face));
		const wxString align = node.GetString(ibProtocolName::Align);
		if (!align.IsEmpty())
			cell.look.SetAlignment(align == wxT("Right") ? wxALIGN_RIGHT : align == wxT("Center") ? wxALIGN_CENTER_HORIZONTAL : wxALIGN_LEFT);
	}
}

//***********************************************************************************
//*                                    Model                                        *
//***********************************************************************************

ibViewTableModel::ibViewTableModel(ibViewFetcher fetcher, ibAnswered answered)
	: m_fetcher(std::move(fetcher)), m_answered(std::move(answered))
{
}

const ibViewTableRow* ibViewTableModel::RowOf(const ibDataViewItem& item)
{
	return item.IsOk() ? static_cast<const ibViewTableRow*>(item.GetID()) : nullptr;
}

long long ibViewTableModel::HandleOf(const ibDataViewItem& item)
{
	const ibViewTableRow* const row = RowOf(item);
	return row != nullptr ? row->GetHandle() : 0;
}

bool ibViewTableModel::IsCellEditable(const ibDataViewItem& item, long long column)
{
	const ibViewTableRow* const row = RowOf(item);
	const ibViewCell* const cell = row != nullptr ? row->FindCell(column) : nullptr;
	return cell != nullptr && cell->editable;
}

void ibViewTableModel::GetValue(wxVariant& variant, const ibDataViewItem& item, unsigned int col) const
{
	const ibViewTableRow* const row = RowOf(item);
	const ibViewCell* const cell = row != nullptr ? row->FindCell(col) : nullptr;
	if (cell != nullptr)
		variant = cell->checked.has_value() ? wxVariant(*cell->checked) : wxVariant(cell->text);
}

bool ibViewTableModel::HasValue(const ibDataViewItem& item, unsigned col) const
{
	const ibViewTableRow* const row = RowOf(item);
	return row != nullptr && row->FindCell(col) != nullptr;
}

bool ibViewTableModel::GetAttr(const ibDataViewItem& item, unsigned int col, ibDataViewItemAttr& attr) const
{
	const ibViewTableRow* const row = RowOf(item);
	const ibViewCell* const cell = row != nullptr ? row->FindCell(col) : nullptr;
	if (cell == nullptr)
		return false;

	// Its look, and a number to the right — unless its look says where it stands.
	ibDataViewItemAttr look = cell->look;
	if (cell->number && !look.HasAlignment())
		look.SetAlignment(wxALIGN_RIGHT);
	if (look.IsDefault())
		return false;
	attr = look;
	return true;
}

bool ibViewTableModel::SetValue(const wxVariant& /*variant*/, const ibDataViewItem& /*item*/, unsigned int /*col*/)
{
	// A cell is written by the server — a Change sent, the rows read again.
	return false;
}

ibDataViewItem ibViewTableModel::GetParent(const ibDataViewItem& item) const
{
	return item.IsOk() ? item.GetParentItem() : ibDataViewItem();
}

unsigned int ibViewTableModel::GetFirstFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor, int count,
	ibDataViewItemArray& out) const
{
	return Fetch(0, parent, anchor, count, out);
}

unsigned int ibViewTableModel::GetNextFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor, int count,
	ibDataViewItemArray& out) const
{
	return Fetch(1, parent, anchor, count, out);
}

unsigned int ibViewTableModel::GetPrevFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor, int count,
	ibDataViewItemArray& out) const
{
	return Fetch(-1, parent, anchor, count, out);
}

ibPictureID ibViewTableModel::GetRowPicture(const ibDataViewItem& item) const
{
	const ibViewTableRow* const row = RowOf(item);
	return row != nullptr ? row->GetPicture() : 0;
}

unsigned int ibViewTableModel::Fetch(int direction, const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibDataViewItemArray& out) const
{
	// The level read: a row opened or entered — none at the top, the flat list's marker included (the server knows
	// its own view mode).
	const ibDataViewItem level = parent == s_constIgnoreParent ? ibDataViewItem() : parent;
	const int asked = count > 0 ? std::min(count, kMaxFetchCount) : kMaxFetchCount;

	ibProtocolNode request, answer;
	request.SetValue(ibProtocolName::Direction, direction)
		.SetValue(ibProtocolName::Anchor, HandleOf(anchor))
		.SetValue(ibProtocolName::Parent, HandleOf(level))
		.SetValue(ibProtocolName::Count, asked);
	if (!m_fetcher(request, answer))
		return 0;

	unsigned int read = 0;
	for (const ibProtocolNode& node : answer.Children()) {
		ibViewTableRow* const row = new ibViewTableRow(node.GetId(), level);
		row->Read(node);
		out.Add(ibDataViewItem(row));
		++read;
	}

	// What the UI does with it — after the read, on its own thread. Handles only: a row's count of owners is no
	// atomic, and the rows are the data view's to hand to the UI.
	ibAnswer done;
	done.currentRow = answer.GetInt(ibProtocolName::CurrentRow);
	for (const ibProtocolNode& picture : answer.FindChild(ibProtocolName::Pictures).Children())
		done.pictures.emplace_back(PictureIdOf(picture.GetString(ibProtocolName::Id)), picture.GetString(ibProtocolName::Picture));

	ibJournalInfo(wxT("table"), wxT("read %d from %lld under %lld, %d asked: %u rows%s, current %lld, %u pictures"),
		direction, HandleOf(anchor), HandleOf(level), asked, read, answer.GetBool(ibProtocolName::End) ? wxT(", the end") : wxT(""),
		done.currentRow, static_cast<unsigned>(done.pictures.size()));
	if (done.currentRow != 0 || !done.pictures.empty())
		m_answered(done);

	// As many as the server's model read — fewer than asked is the end that way, for the data view as for it.
	return read;
}
