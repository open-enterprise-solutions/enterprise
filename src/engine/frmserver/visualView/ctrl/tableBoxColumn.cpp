#include "tableBox.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "form.h"
#include "backend/choiceLinkResolver.h"   // ibChoiceHolder — where this column's link reads its neighbours
#include "backend/backend_picture.h"           // ibBackendPicture::SendPicture — a header picture, by its id
#include "backend/composition/dataComposer.h"   // the composer's active sort — the header's arrow
#include "core/formatString.h"         // ibFormatString — what a cell is shown through
#include "core/inputMask.h"            // ibInputMask — what can be typed into it
#include "backend/appData.h"
#include "backend/metaData.h"

#include <algorithm>   // std::clamp — a width by type, between its floor and its cap

//****************************************************************************
//*                       the view's child, as the column                    *
//****************************************************************************

ibString ibColumnViewProvider::GetClassName() const
{
	return m_column->GetClassName();
}

ibString ibColumnViewProvider::GetObjectTypeName() const
{
	return m_column->GetObjectTypeName();
}

bool ibColumnViewProvider::IsEditable() const
{
	return m_column->IsEditable();
}

const ibMetaData* ibColumnViewProvider::GetMetaData() const
{
	return m_column->GetMetaData();
}

// A BOOLEAN'S TRUTH IS THE VALUE'S, not its text's: an Input draws it as a tick and edits it with a check box (Max,
// 2026-11-24: "a boolean opens a boolean editor, not a text one"), a Flag as its box.
void ibColumnViewProvider::UpdateCell(const ibVariant& value, ibDataNode& cell) const
{
	if (value.GetType() == ibValueTypes::TYPE_BOOLEAN)
		cell.SetValue(ibProtocol::ibForm::ibTable::Checked, value.GetBoolean());
}

// A view with no format of its own: the cells show through the one its model column gives (GetCellFormat).
const ibTranslateString& ibColumnViewProvider::GetFormat() const
{
	static const ibTranslateString s_none;
	return s_none;
}

// …and the mask written on its model column (the attribute's), else the one its type gives: a string is shown through it
// in any view.
ibInputMask ibColumnViewProvider::GetMask() const
{
	const ibValueModelTableBox* owner = m_column->GetOwner();
	const ibValueModel* model = owner != nullptr ? owner->GetTableModel() : nullptr;
	const ibValueModel::ibValueModelColumnCollection* columns = model != nullptr ? model->GetColumnCollection() : nullptr;
	const ibString text = columns != nullptr ? columns->GetColumnMask(m_column->GetModelColumn()) : ibString();

	ibInputMask mask;
	if (!text.IsEmpty())
		mask = ibInputMask::Parse(text);
	else
		ibInputMask::FromTypeDesc(m_column->GetTypeDesc(), mask);
	return mask;
}

// A view that is not typed into answers the choice with what the column's type and its attribute say — the base answers
// the column's own doors would give, were the view not asked first.
bool ibColumnViewProvider::HasQuickChoice() const
{
	return m_column->ibValueControl::HasQuickChoice();
}

ibSelectMode ibColumnViewProvider::GetSelectMode() const
{
	return m_column->ibTypeControlFactory::GetSelectMode();
}

const ibChoiceTypeLinkDescription& ibColumnViewProvider::GetTypeLink() const
{
	return m_column->ibControlFrame::GetTypeLink();
}

const ibChoiceParametersDescription& ibColumnViewProvider::GetChoiceParameters() const
{
	return m_column->ibControlFrame::GetChoiceParameters();
}

ibChoiceCondition ibColumnViewProvider::GetChoiceCondition() const
{
	return m_column->ibTypeControlFactory::GetChoiceCondition();
}

std::unique_ptr<ibColumnViewProvider> ibColumnViewProvider::Create(ibColumnView view, ibValueModelTableBoxColumn* column)
{
	switch (view) {
	case ibColumnView::ibColumnView_Input: return std::make_unique<ibColumnViewInput>(column);
	case ibColumnView::ibColumnView_Flag:  return std::make_unique<ibColumnViewFlag>(column);
	case ibColumnView::ibColumnView_Label: return std::make_unique<ibColumnViewLabel>(column);
	default:                               return nullptr;   // a number no view has
	}
}

//***********************************************************************************
//*                            ibValueModelTableBoxColumn                                 *
//***********************************************************************************

ibValueModelTableBoxColumn::ibValueModelTableBoxColumn() :
	ibValueControl(), ibTypeControlFactory(), m_model_id(ibNOT_FOUND)
{
	// born typed into
	SetView(ibColumnView::ibColumnView_Input);
}

// THE TABLE THIS COLUMN SERVES — asked of its PARENT, one step at a time. A table answers
// with itself; a group asks ITS holder, and that is the whole walk: no loop, and no depth to
// know, because each node answers only for the step it can see.
ibValueModelTableBox* ibValueModelTableBoxColumn::GetOwner() const
{
	if (ibValueModelTableBox* table = dynamic_cast<ibValueModelTableBox*>(m_parent))
		return table;
	if (ibValueModelTableBoxColumnGroup* group = dynamic_cast<ibValueModelTableBoxColumnGroup*>(m_parent))
		return group->GetOwner();
	return nullptr;
}

bool ibValueModelTableBoxColumn::GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const
{
	// #3 — the table-COLUMN source list = the current table (what the parent tablebox offers: the
	// tabular sections; the picker roots the bound one and lists its columns) PLUS the form's object
	// attributes (exactly what the ATTRIBUTE picker returns) — so a column can also bind a value from
	// the object ABOVE the table (its header), living alongside the table's own columns (Mode 2).
	// The parent tablebox may be transiently DETACHED mid-rebuild — a deferred inspector query can reach a
	// just-orphaned column (GetOwner() == null). Skip it gracefully instead of dereferencing null. The
	// header object still contributes.
	bool ok = false;
	if (auto* owner = GetOwner())
		ok = owner->GetSourceList(out);                            // current table
	if (m_formOwner != nullptr)
		ok = m_formOwner->GetSourceList(ibSourceDataType::ibSourceDataType_attribute, out) || ok;   // + header object
	return ok;
}

const ibMetaData* ibValueModelTableBoxColumn::GetMetaData() const
{
	return m_formOwner ?
		m_formOwner->GetMetaData() : nullptr;
}

ibString ibValueModelTableBoxColumn::GetControlTitle() const
{
	// Explicit Title wins; otherwise the synonym of the column the binding reaches (WalkSource: a field, or the form's
	// attribute bound whole); else fall back to the control's own name.
	if (!m_propertyTitle->IsEmptyProperty()) {
		return m_propertyTitle->GetValueAsTranslateString();
	}
	else if (!m_propertySource->IsEmptyProperty()) {
		const ibBackendSourceColumn* column = WalkSource(GetSourceDesc());
		if (column != nullptr)   // null when the bound field is gone
			return column->GetSynonym();
	}

	return m_propertyName->GetValueAsString();
}

bool ibValueModelTableBoxColumn::IsColumnShown() const
{
	// A source-less column is not shown — no binding PATH (m_propertySource) and no explicit model column
	// (m_model_id). It appears once the user binds a source (a field, or a dotted path), mirroring the
	// unbound-source-control visibility gate. It stays selectable via the object tree while hidden, so its
	// Source picker is reachable. (GetModelColumn can't gate this — it falls back to the control id.)
	const bool sourceMissing = m_propertySource->IsEmptyProperty() && m_model_id == ibNOT_FOUND;
	// …nor is a column the feature options of this base make unavailable (its field, or the options it names).
	if (!m_propertyVisible->GetValueAsBoolean() || sourceMissing || !IsAvailable())
		return false;

	// …nor one inside a group that is hidden: a hidden group takes its columns with it.
	for (const ibValueFrame* holder = m_parent; holder != nullptr; holder = holder->GetParent()) {
		const ibValueModelTableBoxColumnGroup* group = dynamic_cast<const ibValueModelTableBoxColumnGroup*>(holder);
		if (group == nullptr)
			break;   // the table — the top of the column tree
		if (!group->IsGroupShown())
			return false;
	}
	return true;
}

bool ibValueModelTableBoxColumn::IsReadOnly() const
{
	// A view that edits nothing (a Label), or a view-only form: nothing on it is edited.
	if (m_provider->IsReadOnly() || ibValueControl::IsReadOnly())
		return true;

	const ibValueModelTableBox* owner = GetOwner();
	if (owner == nullptr)
		return false;

	// A dot-path OR a foreign-root (header) column is read-only — its value is resolved through the
	// dot / the form, not stored, so it can't be written back (the cell still shows the resolved text).
	if (owner->IsPathColumn(this) || owner->IsForeignColumn(this))
		return true;

	// …and the table's own binding: the object it is a section of may not be changed by this person, or
	// is read through a reference — the same question every bound control asks (IsWritableBinding).
	const ibSourceDescription& tableDesc = owner->GetSourceDesc();
	return tableDesc.IsOk() && m_formOwner != nullptr && !m_formOwner->IsWritableBinding(tableDesc);
}

void ibValueModelTableBoxColumn::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	// A VIEW THE COLUMN'S TYPE DOES NOT ALLOW — set so, left so by an attribute retyped in the metadata, read so — gives
	// way to Input here, as the field's does (a source changed does it at once, OnPropertyChanged); through the door, so
	// the view's change runs its own course.
	if (!IsViewAllowed(GetView()))
		ibPropertyGate::SetValue(this, m_propertyView, ibVariant(static_cast<int>(ibColumnView::ibColumnView_Input)));

	// The representation the header and the footer are drawn in — Auto shows both, the title and the
	// picture, whichever are set. The caption is always written: it names the column even when only the
	// picture is shown, and the representation says which of the two are.
	ibRepresentation rep = m_propertyRepresentation->GetValueAsEnum();
	if (rep == ibRepresentation::ibRepresentation_Auto)
		rep = ibRepresentation::ibRepresentation_PictureAndText;

	const ibString caption = GetControlTitle();
	state.SetValue(ibProtocol::ibForm::ibState::Caption, caption);
	state.SetValue(ibProtocol::ibForm::ibTable::FooterText, m_propertyFooterText->GetValueAsTranslateString());

	// WITH NO WIDTH SET, THE CHARACTERS ITS TYPE NEEDS — answered here, as its caption and its format are: the type and
	// the format it is printed through are the server's. At least the caption's longest line, so the header is not cut,
	// and at most 50, so one wide field does not take the table. A client turns them into pixels with its own font.
	if (GetWidthColumn() == 0) {
		ibFormatString typeFormat;
		const ibFormatString* format = GetCellFormat();
		if (format == nullptr)
			ibFormatString::FromTypeDesc(GetTypeDesc(), typeFormat);
		unsigned int chars = ibBackendTypeApplicationFactory::GetWidthFromColumn(format != nullptr ? *format : typeFormat,
			GetTypeDesc());
		unsigned int line = 0;
		for (const ibChar c : caption) {
			line = c == ibT('\n') ? 0 : line + 1;
			chars = std::max(chars, line);
		}
		state.SetValue(ibProtocol::ibForm::ibTable::Chars, static_cast<std::int32_t>(std::clamp(chars, 3u, 50u)));
	}
	state.SetValue(ibProtocol::ibForm::ibState::Representation, static_cast<std::int32_t>(rep));
	if (rep != ibRepresentation::ibRepresentation_Text) {
		const ibDataValue header = ibBackendPicture::SendPicture(m_propertyHeaderPicture->GetValueAsPictureDesc(), GetMetaData());
		if (!header.IsEmpty())
			state.SetValue(ibProtocol::ibForm::ibTable::HeaderPicture, header);
		const ibDataValue footer = ibBackendPicture::SendPicture(m_propertyFooterPicture->GetValueAsPictureDesc(), GetMetaData());
		if (!footer.IsEmpty())
			state.SetValue(ibProtocol::ibForm::ibTable::FooterPicture, footer);
	}

	// Its own Visible, a binding, the feature options — the group above says its own (its Visible).
	const bool sourceMissing = m_propertySource->IsEmptyProperty() && m_model_id == ibNOT_FOUND;
	state.SetValue(ibProtocol::ibForm::ibState::Visible, m_propertyVisible->GetValueAsBoolean() && !sourceMissing && IsAvailable());
	state.SetValue(ibProtocol::ibForm::ibTable::ReadOnly, IsReadOnly());
	// What can be typed into its cells — the mask in effect, resolved here, as the field's is.
	const ibInputMask mask = GetMask();
	if (!mask.IsEmpty())
		state.SetValue(ibProtocol::ibForm::ibTable::Mask, mask.Render());
	// …and the text a value just written into the cell reads as, said once (m_editText).
	if (m_editText) {
		state.SetValue(ibProtocol::ibForm::ibTable::EditText, *m_editText);
		m_editText.reset();
	}

	// Sortability is gated by the model's Sorting FEATURE (when the flag is turned off, sorting cannot be
	// used), and by a field to sort by. The ACTIVE sort + direction come from L5 — the composer — matched
	// by the column's OWN bound field (GetSourceFieldName, the same string OnColumnClick commits); no model
	// column-id resolution (a reference dot-path field is not a model column id). So a sort set in the
	// settings window shows on the header as surely as one set by a click.
	const ibValueModelTableBox* owner = GetOwner();
	ibValueModel* model = owner != nullptr ? owner->GetTableModel() : nullptr;
	const ibString field = GetSourceFieldName();
	const bool sortable = model != nullptr && !appData->DesignerMode() && !field.IsEmpty()
		&& model->GetFeatures().Has(ibValueModel::Features::Sorting);
	state.SetValue(ibProtocol::ibForm::ibTable::Sortable, sortable);
	if (sortable) {
		const ibDataComposer& composer = model->GetModelComposer();
		for (size_t i = 0; i < composer.SortCount(); ++i) {
			ibString sortField; bool asc = true;
			if (composer.GetSortAt(i, sortField, asc) && sortField == field) {
				const std::int32_t sort = asc ? 1 : -1;
				state.SetValue(ibProtocol::ibForm::ibTable::Sort, sort);
				break;
			}
		}
	}
}

const ibFormatString* ibValueModelTableBoxColumn::GetCellFormat() const
{
	const ibTranslateString& format = GetFormat();
	if (!format.IsEmpty())
		return &ibBackendTypeApplicationFactory::GetFormatFromColumn(format, GetTypeDesc());
	const ibValueModelTableBox* owner = GetOwner();
	const ibValueModel* model = owner != nullptr ? owner->GetTableModel() : nullptr;
	const ibValueModel::ibValueModelColumnCollection* columns = model != nullptr ? model->GetColumnCollection() : nullptr;
	return columns != nullptr ? &columns->GetColumnFormat(GetModelColumn()) : nullptr;
}

ibInputMask ibValueModelTableBoxColumn::GetMask() const
{
	return m_provider->GetMask();
}

ibString ibValueModelTableBoxColumn::GetEditText(const ibVariant& value) const
{
	if (value.GetType() == ibValueTypes::TYPE_STRING)
		return value.GetString();
	if (value.GetType() == ibValueTypes::TYPE_DATE && value.IsEmpty())
		return ibString();
	static const ibTranslateString s_typeFormat;   // none written: the type's own, as the field takes it
	ibString text;
	ibBackendTypeApplicationFactory::GetFormatFromColumn(s_typeFormat, GetTypeDesc()).Apply(value, text);
	return text;
}

void ibValueModelTableBoxColumn::UpdateCell(const ibDataViewItem& item, ibDataNode& cell) const
{
	UpdateCell(item, cell, GetCellFormat(), GetMask(), IsReadOnly());
}

void ibValueModelTableBoxColumn::UpdateCell(const ibDataViewItem& item, ibDataNode& cell, const ibFormatString* format,
	const ibInputMask& mask, bool readOnly) const
{
	const ibValueModelTableBox* owner = GetOwner();
	ibValueModel* model = owner != nullptr ? owner->GetTableModel() : nullptr;
	if (model == nullptr)
		return;

	const unsigned int modelColumn = static_cast<unsigned int>(GetModelColumn());

	// HOW IT LOOKS — the conditional appearance the model gives the cell (GetAttr), what of it is set: the client lays
	// it over the table's own look, as the data view did.
	ibDataViewItemAttr attr;
	if (model->GetAttr(item, modelColumn, attr) && !attr.IsDefault()) {
		if (attr.HasColour())
			cell.SetValue(ibProtocol::ibForm::ibTable::TextColour, attr.GetColour().ToString());
		if (attr.HasBackgroundColour())
			cell.SetValue(ibProtocol::ibForm::ibTable::BackgroundColour, attr.GetBackgroundColour().ToString());
		if (attr.GetBold())
			cell.SetValue(ibProtocol::ibForm::ibTable::Bold, true);
		if (attr.GetItalic())
			cell.SetValue(ibProtocol::ibForm::ibTable::Italic, true);
		if (attr.GetStrikethrough())
			cell.SetValue(ibProtocol::ibForm::ibTable::Strikethrough, true);
		if (attr.GetUnderlined())
			cell.SetValue(ibProtocol::ibForm::ibTable::Underlined, true);
		if (attr.GetPointSize() > 0)
			cell.SetValue(ibProtocol::ibForm::ibTable::Size, static_cast<std::int32_t>(attr.GetPointSize()));
		if (!attr.GetFaceName().IsEmpty())
			cell.SetValue(ibProtocol::ibForm::ibTable::Face, attr.GetFaceName());
		if (attr.HasAlignment()) {
			const int align = attr.GetAlignment();
			cell.SetValue(ibProtocol::ibForm::ibTable::Align, ibString((align & ibAlignment_Right) ? ibProtocol::ibForm::ibTable::ibAlign::Right
				: (align & ibAlignment_CenterHorizontal) ? ibProtocol::ibForm::ibTable::ibAlign::Center : ibProtocol::ibForm::ibTable::ibAlign::Left));
		}
	}

	// The per-cell value resolves THROUGH this column's binding. A dot-path column ("Counterparty.Supplier")
	// or a header column is resolved by the table — first hop via the dumb model, deeper hops walk the
	// reference; a plain column is the model's own. Whether the cell has one at all (a container's non-first
	// columns) the fetch has asked already. Each read stays what it was read as; the cell only looks at it.
	ibValue resolved;
	ibVariant read;
	const bool walked = owner->ResolveCellValue(item, this, resolved);
	if (!walked)
		model->GetValue(read, item, modelColumn);
	const ibVariant& value = walked ? resolved : read;

	if (value.GetType() == ibValueTypes::TYPE_EMPTY) {
		cell.SetValue(ibProtocol::ibForm::ibTable::Text, ibString());
		return;
	}

	// Everything below is asked of the value itself, through the format of the column — a string through its mask, as
	// a field shows one; an empty date under a mask is a blank cell, not 01.01.0001.
	ibString text;
	if (!mask.IsEmpty() && value.GetType() == ibValueTypes::TYPE_STRING)
		text = mask.Apply(value.GetString());
	else if (!mask.IsEmpty() && value.GetType() == ibValueTypes::TYPE_DATE && value.IsEmpty())
		text.clear();
	else if (format != nullptr)
		format->Apply(value, text);
	else
		text = value.GetString();
	cell.SetValue(ibProtocol::ibForm::ibTable::Text, text);
	// …and, in a cell the client may edit, the value with its type (ibValue::Serialize) — the field's way. A list's
	// cells, never edited, carry their text alone and stay as light as they were.
	if (!readOnly && model->EditableLine(item, modelColumn)) {
		if (value.IsTransferable())
			value.Serialize(cell.Child(ibProtocol::ibForm::ibTable::Value));
		// Under a mask, where the text is not the one the mask is typed over (a long date, digit groups): the text the
		// cell's editor starts from.
		if (!mask.IsEmpty() && !text.IsEmpty() && value.GetType() != ibValueTypes::TYPE_STRING) {
			const ibString editText = GetEditText(value);
			if (editText != text)
				cell.SetValue(ibProtocol::ibForm::ibTable::EditText, editText);
		}
	}

	// A number stands to the right…
	if (value.GetType() == ibValueTypes::TYPE_NUMBER)
		cell.SetValue(ibProtocol::ibForm::ibTable::Number, true);
	// …and the view adds what it draws by.
	m_provider->UpdateCell(value, cell);
}

bool ibValueModelTableBoxColumn::IsCellEditable(const ibDataViewItem& item) const
{
	if (!item.IsOk() || IsReadOnly())
		return false;
	const ibValueModelTableBox* owner = GetOwner();
	ibValueModel* model = owner != nullptr ? owner->GetTableModel() : nullptr;
	return model != nullptr && model->EditableLine(item, static_cast<unsigned int>(GetModelColumn()));
}

bool ibValueModelTableBoxColumn::CanDeleteControl() const
{
	return m_parent->GetChildCount() > 1;
}

#include "backend/metaCollection/partial/commonObject.h"

//*******************************************************************
//*							 Control value	                        *
//*******************************************************************

bool ibValueModelTableBoxColumn::SetControlValue(const ibValue& varControlVal)
{
	// A dot-path or a foreign-root (header) column is resolved, not stored (IsReadOnly): there is no cell to write.
	const ibValueModelTableBox* owner = GetOwner();
	if (owner != nullptr && (owner->IsPathColumn(this) || owner->IsForeignColumn(this)))
		return false;

	// Written into the CURRENT ROW's cell; the client reads it back with the next fetch of that row — narrowed first by
	// the column's own link by type (#616), as a form field's is (ibValueField::SetControlValue).
	ibValue value = varControlVal;
	ibValue adjusted;
	if (ibChoiceLinkResolver::Adjust(ibChoiceHolder(m_formOwner), GetTypeLink(), varControlVal, adjusted))
		value = adjusted;

	ibValue before;
	GetControlValue(before);

	ibValueModel::ibValueModelReturnLine* currentLine = GetCurrentLine();
	if (currentLine != nullptr)
		currentLine->SetValueByMetaID(GetModelColumn(), value);

	// …and into the cell's open editor, as the desktop's column wrote it there: the text the edit's end sends back — under
	// a mask, the one the mask is typed over (a cleared date: its skeleton).
	m_editText = GetMask().IsEmpty() ? value.GetString() : GetEditText(value);

	// …and the controls chosen within this cell are emptied, on a change (#616) — the other cells of its row among them.
	ibTypeControlFactory::ClearLinked(this, before);

	if (m_formOwner != nullptr)
		m_formOwner->RefreshForm();
	return true;
}

// ⭐⭐ A COLUMN STANDS IN TWO PLACES AT ONCE, and a link may name a field in either: the other cells of
// THE ROW it is editing, and the attributes of the OBJECT above the section. So the holder is given
// both — the row it is in, and the source the form is bound to — and the reading tries them in that
// order (choiceLinkResolver.cpp). A link by type is simply the choice of a field, of the tabular
// section or of the header (Max, 2026-09-23).
ibChoiceHolder ibValueModelTableBoxColumn::GetChoiceHolder() const
{
	ibValueModel::ibValueModelReturnLine* currentLine = GetCurrentLine();
	if (currentLine == nullptr)
		return m_formOwner != nullptr ? ibChoiceHolder(m_formOwner->GetSourceObject()) : ibChoiceHolder();

	ibChoiceHolder holder(currentLine->GetOwnerModel(), currentLine->GetLineItem());
	if (m_formOwner != nullptr)
		holder.m_source = m_formOwner->GetSourceObject();   // …and the header, for a link that names it
	return holder;
}

// …the rest of the choice is the view's.
bool ibValueModelTableBoxColumn::HasQuickChoice() const
{
	return m_provider->HasQuickChoice();
}

ibSelectMode ibValueModelTableBoxColumn::GetSelectMode() const
{
	return m_provider->GetSelectMode();
}

const ibChoiceTypeLinkDescription& ibValueModelTableBoxColumn::GetTypeLink() const
{
	return m_provider->GetTypeLink();
}

const ibChoiceParametersDescription& ibValueModelTableBoxColumn::GetChoiceParameters() const
{
	return m_provider->GetChoiceParameters();
}

ibChoiceCondition ibValueModelTableBoxColumn::GetChoiceCondition() const
{
	return m_provider->GetChoiceCondition();
}

bool ibValueModelTableBoxColumn::GetControlValue(ibValue& pvarControlVal) const
{
	ibValueModel::ibValueModelReturnLine* currentLine = GetCurrentLine();
	if (currentLine != nullptr) {
		return currentLine->GetValueByMetaID(
			GetModelColumn(), pvarControlVal
		);
	}

	return false;
}

//*******************************************************************
//*							 The view    	                        *
//*******************************************************************

void ibValueModelTableBoxColumn::SetView(ibColumnView view)
{
	std::unique_ptr<ibColumnViewProvider> made = ibColumnViewProvider::Create(view, this);
	if (made == nullptr) {
		// A number no view has, or a view half added: the column is kept, typed into.
		m_propertyView->SetValue(ibColumnView::ibColumnView_Input);
		made = ibColumnViewProvider::Create(ibColumnView::ibColumnView_Input, this);
	}

	if (m_provider != nullptr) {
		// WHAT THE TWO VIEWS HAVE BY THE SAME NAME CROSSES OVER — a handler on OnChange stays on OnChange. The old view's
		// node read by the new one carries the properties and the handlers' names…
		ibDataNode carried;
		m_provider->WriteData(carried);
		made->ReadData(carried);
		// …and a lambda a script hung on, which a node does not carry.
		for (unsigned int idx = 0; idx < made->GetEventCount(); idx++) {
			ibEvent* const event = made->GetEvent(idx);
			const ibEvent* const was = event != nullptr ? m_provider->GetEvent(event->GetName()) : nullptr;
			ibValue handler;
			if (was != nullptr && was->GetDataValue(handler))
				event->SetDataValue(handler);
		}
		RemoveAttachedObject(m_provider.get());
	}

	m_provider = std::move(made);
	AttachPropertyObject(m_provider.get());

	// The members a script names are the new view's.
	InvalidateNames();
}

//***********************************************************************************
//*                                  Data											*
//***********************************************************************************

bool ibValueModelTableBoxColumn::ReadData(const ibDataNode& node)
{
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyRepresentation->SetNodeValue(node.GetProperty(m_propertyRepresentation->GetName()));
	m_propertyFooterText->SetNodeValue(node.GetProperty(m_propertyFooterText->GetName()));
	m_propertyHeaderPicture->SetNodeValue(node.GetProperty(m_propertyHeaderPicture->GetName()));
	m_propertyFooterPicture->SetNodeValue(node.GetProperty(m_propertyFooterPicture->GetName()));
	m_propertyHeaderAlign->SetNodeValue(node.GetProperty(m_propertyHeaderAlign->GetName()));
	m_propertyFooterAlign->SetNodeValue(node.GetProperty(m_propertyFooterAlign->GetName()));
	m_propertyWidth->SetNodeValue(node.GetProperty(m_propertyWidth->GetName()));
	m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));
	m_propertyResizable->SetNodeValue(node.GetProperty(m_propertyResizable->GetName()));
	//m_propertySortable->SetNodeValue(node.GetProperty(m_propertySortable->GetName()));
	m_propertyReorderable->SetNodeValue(node.GetProperty(m_propertyReorderable->GetName()));
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));

	// A view this version does not know (a later one, or a damaged form) is refused by the property, which keeps the one
	// it holds: the column is kept, typed into, and the number is said.
	const auto saved = node.FindProperty(m_propertyView->GetName());
	if (saved && !m_propertyView->SetNodeValue(*saved)) {
		ibJournalWarning(ibT("ui.form"), _("Column '%s' has a view this version does not know (%d); it is shown as Input."),
			GetControlName(), (int)saved->AsInt());
		m_propertyView->SetValue(ibColumnView::ibColumnView_Input);
	}
	SetView(GetView());

	if (!m_provider->ReadData(node))
		return false;

	return ibValueControl::ReadData(node);
}

bool ibValueModelTableBoxColumn::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyView->GetName(), m_propertyView->GetNodeValue());
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyRepresentation->GetName(), m_propertyRepresentation->GetNodeValue());
	node.SetProperty(m_propertyFooterText->GetName(), m_propertyFooterText->GetNodeValue());
	node.SetProperty(m_propertyHeaderPicture->GetName(), m_propertyHeaderPicture->GetNodeValue());
	node.SetProperty(m_propertyFooterPicture->GetName(), m_propertyFooterPicture->GetNodeValue());
	node.SetProperty(m_propertyHeaderAlign->GetName(), m_propertyHeaderAlign->GetNodeValue());
	node.SetProperty(m_propertyFooterAlign->GetName(), m_propertyFooterAlign->GetNodeValue());
	node.SetProperty(m_propertyWidth->GetName(), m_propertyWidth->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());
	node.SetProperty(m_propertyResizable->GetName(), m_propertyResizable->GetNodeValue());
	//node.SetProperty(m_propertySortable->GetName(), m_propertySortable->GetNodeValue());
	node.SetProperty(m_propertyReorderable->GetName(), m_propertyReorderable->GetNodeValue());
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());

	if (!m_provider->WriteData(node))
		return false;

	return ibValueControl::WriteData(node);
}

// The copy walk (ibValueFrame::CopyNode) carries every property and event, the view's among them; the paste makes the
// column of the view pasted first, and its child takes what the copy carried.
bool ibValueModelTableBoxColumn::PasteData(const ibDataNode& node)
{
	SetView(GetView());
	return m_provider->PasteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(ibValueModelTableBoxColumn, "TableColumn", "TableColumn", g_controlTableBoxColumnCLSID);