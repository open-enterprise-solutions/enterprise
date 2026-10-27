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

// A picture's id as the wire writes it — a decimal string: a std::uint64_t is past what a JSON number keeps.
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

bool ibViewTableRow::GetGroupCaption(ibString& caption) const
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
	m_container = row.GetBool(ibProtocol::ibForm::ibTable::Container);
	m_group = row.GetString(ibProtocol::ibForm::ibTable::Group);
	m_picture = PictureIdOf(row.GetString(ibProtocol::ibForm::ibTable::Picture));

	for (const ibProtocolNode& node : row.Children()) {
		ibViewCell& cell = m_cells[node.GetId()];
		cell.text = node.GetString(ibProtocol::ibForm::ibTable::Text);
		if (node.Has(ibProtocol::ibForm::ibTable::Checked))
			cell.checked = node.GetBool(ibProtocol::ibForm::ibTable::Checked);
		cell.number = node.GetBool(ibProtocol::ibForm::ibTable::Number);
		cell.editable = node.Has(ibProtocol::ibForm::ibTable::Value);

		// Its look — what of it the server set, its colours in the engine's text of a colour (ibColour, as a form's).
		ibColour colour;
		if (colour.FromString(node.GetString(ibProtocol::ibForm::ibTable::TextColour)))
			cell.look.SetColour(colour);
		ibColour background;
		if (background.FromString(node.GetString(ibProtocol::ibForm::ibTable::BackgroundColour)))
			cell.look.SetBackgroundColour(background);
		cell.look.SetBold(node.GetBool(ibProtocol::ibForm::ibTable::Bold));
		cell.look.SetItalic(node.GetBool(ibProtocol::ibForm::ibTable::Italic));
		cell.look.SetStrikethrough(node.GetBool(ibProtocol::ibForm::ibTable::Strikethrough));
		cell.look.SetUnderlined(node.GetBool(ibProtocol::ibForm::ibTable::Underlined));
		cell.look.SetPointSize(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibTable::Size)));
		cell.look.SetFaceName(node.GetString(ibProtocol::ibForm::ibTable::Face));
		const wxString align = node.GetString(ibProtocol::ibForm::ibTable::Align);
		if (!align.IsEmpty())
			cell.look.SetAlignment(align == ibProtocol::ibForm::ibTable::ibAlign::Right ? wxALIGN_RIGHT
				: align == ibProtocol::ibForm::ibTable::ibAlign::Center ? wxALIGN_CENTER_HORIZONTAL : wxALIGN_LEFT);
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

void ibViewTableModel::GetValue(ibVariant& variant, const ibDataViewItem& item, unsigned int col) const
{
	const ibViewTableRow* const row = RowOf(item);
	const ibViewCell* const cell = row != nullptr ? row->FindCell(col) : nullptr;
	if (cell != nullptr)
		variant = cell->checked.has_value() ? ibVariant(*cell->checked) : ibVariant(cell->text);
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

bool ibViewTableModel::SetValue(const ibVariant& /*variant*/, const ibDataViewItem& /*item*/, unsigned int /*col*/)
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
	return Fetch(ibProtocol::ibForm::ibTable::ibDirection::Reset, parent, anchor, count, out);
}

unsigned int ibViewTableModel::GetNextFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor, int count,
	ibDataViewItemArray& out) const
{
	return Fetch(ibProtocol::ibForm::ibTable::ibDirection::Forward, parent, anchor, count, out);
}

unsigned int ibViewTableModel::GetPrevFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor, int count,
	ibDataViewItemArray& out) const
{
	return Fetch(ibProtocol::ibForm::ibTable::ibDirection::Backward, parent, anchor, count, out);
}

unsigned int ibViewTableModel::GetAroundFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor, int count,
	int before, ibDataViewItemArray& out, unsigned int& above) const
{
	const unsigned int read = Fetch(ibProtocol::ibForm::ibTable::ibDirection::Reset, parent, anchor, count, before, out,
		above);
	return read - above;
}

ibPictureID ibViewTableModel::GetRowPicture(const ibDataViewItem& item) const
{
	const ibViewTableRow* const row = RowOf(item);
	return row != nullptr ? row->GetPicture() : 0;
}

unsigned int ibViewTableModel::Fetch(ibProtocol::ibForm::ibTable::ibDirection direction, const ibDataViewItem& parent,
	const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const
{
	unsigned int above = 0;
	return Fetch(direction, parent, anchor, count, 0, out, above);
}

unsigned int ibViewTableModel::Fetch(ibProtocol::ibForm::ibTable::ibDirection direction, const ibDataViewItem& parent,
	const ibDataViewItem& anchor, int count, int before, ibDataViewItemArray& out, unsigned int& above) const
{
	above = 0;

	// The level read: a row opened or entered — none at the top, the flat list's marker included (the server knows
	// its own view mode).
	const ibDataViewItem level = parent == s_constIgnoreParent ? ibDataViewItem() : parent;
	const int asked = count > 0 ? std::min(count, kMaxFetchCount) : kMaxFetchCount;

	ibProtocolNode request, answer;
	request.SetValue(ibProtocol::ibForm::ibTable::Direction, static_cast<int>(direction))
		.SetValue(ibProtocol::ibForm::ibTable::Anchor, HandleOf(anchor))
		.SetValue(ibProtocol::ibForm::ibTable::Parent, HandleOf(level))
		.SetValue(ibProtocol::ibForm::ibTable::Count, asked);
	if (before > 0)
		request.SetValue(ibProtocol::ibForm::ibTable::Before, std::min(before, kMaxFetchCount));
	if (!m_fetcher(request, answer))
		return 0;

	unsigned int read = 0;
	for (const ibProtocolNode& node : answer.Children()) {
		ibViewTableRow* const row = new ibViewTableRow(node.GetId(), level);
		row->Read(node);
		out.Add(ibDataViewItem(row));
		++read;
	}
	above = std::min(static_cast<unsigned int>(answer.GetInt(ibProtocol::ibForm::ibTable::Before)), read);

	// What the UI does with it — after the read, on its own thread. Handles only: a row's count of owners is no
	// atomic, and the rows are the data view's to hand to the UI.
	ibAnswer done;
	done.currentRow = answer.GetInt(ibProtocol::ibForm::ibTable::CurrentRow);

	ibJournalInfo(wxT("table"), wxT("read %d from %lld under %lld, %d asked: %u rows%s, current %lld"),
		static_cast<int>(direction), HandleOf(anchor), HandleOf(level), asked, read, answer.GetBool(ibProtocol::ibForm::ibTable::End) ? wxT(", the end") : wxT(""),
		done.currentRow);
	if (done.currentRow != 0)
		m_answered(done);

	// As many as the server's model read — fewer than asked is the end that way, for the data view as for it.
	return read;
}
