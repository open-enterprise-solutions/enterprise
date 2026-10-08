#include "tableBox.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "form.h"
#include "backend/choiceLinkResolver.h"   // ibChoiceHolder — where this column's link reads its neighbours
#include "frmserver/visualView/visualHost.h"   // ibVisualHost::SendPicture — a header picture, by its id
#include "backend/composition/dataComposer.h"   // the composer's active sort — the header's arrow
#include "core/formatString.h"         // ibFormatString — what a cell is shown through
#include "backend/appData.h"

//****************************************************************************
#include "backend/metaData.h"
#include "backend/objCtor.h"

bool ibValueModelTableBoxColumn::GetChoiceForm(ibPropertyList* property)
{
	const ibMetaData* metaData = GetMetaData();
	if (metaData != nullptr) {
		const ibValueMetaObjectRecordDataRef* metaObjectRefValue = nullptr;
		if (!m_propertySource->IsEmptyProperty()) {
			// Resolve the bound field through the source explorer (WalkSource), NOT the form's own metaobject:
			// a dotted path's leaf — or a value-table / dynamic-list column with NO backing metaobject — is
			// missed by a source-scoped FindAnyObjectByFilter (and asserts). Mirror ibValueTextCtrl::GetChoiceForm.
			const ibBackendSourceColumn* column = m_propertySource->GetSourceAttributeObject();
			if (column != nullptr) {
				const ibCtorMetaValueType* so = metaData->GetTypeCtor(column->GetTypeDesc().GetFirstClsid());
				if (so != nullptr)
					metaObjectRefValue = dynamic_cast<const ibValueMetaObjectRecordDataRef*>(so->GetMetaObject());
			}
		}
		else {
			const ibCtorMetaValueType* so = metaData->GetTypeCtor(ibTypeControlFactory::GetFirstClsid());
			if (so != nullptr) {
				metaObjectRefValue = dynamic_cast<const ibValueMetaObjectRecordDataRef*>(so->GetMetaObject());
			}
		}

		if (metaObjectRefValue != nullptr) {
			for (auto form : metaObjectRefValue->GetFormArrayObject()) {
				property->AppendItem(
					form->GetSynonym(),
					form->GetMetaID(),
					form->GetIcon(),
					form
				);
			}
		}
	}

	return true;
}

//***********************************************************************************
//*                            ibValueModelTableBoxColumn                                 *
//***********************************************************************************

ibValueModelTableBoxColumn::ibValueModelTableBoxColumn() :
	ibValueControl(), ibTypeControlFactory(), m_model_id(wxNOT_FOUND)
{
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

wxString ibValueModelTableBoxColumn::GetControlTitle() const
{
	// Explicit Title wins; otherwise the binding's presentation synonym (field synonym | form-attribute
	// Synonym/Name), resolved once via GetSourceAbstractColumn; else fall back to the control's own name.
	if (!m_propertyTitle->IsEmptyProperty()) {
		return m_propertyTitle->GetValueAsTranslateString();
	}
	else if (!m_propertySource->IsEmptyProperty()) {
		const ibBackendAbstractColumn* column = GetSourceAbstractColumn();
		if (column != nullptr)   // null when the bound field is gone / whole-attribute binding
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
	const bool sourceMissing = m_propertySource->IsEmptyProperty() && m_model_id == wxNOT_FOUND;
	// …nor is a column the functional options of this base make unavailable (its field, or the options it names).
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
	// A view-only form: nothing on it is edited.
	if (ibValueControl::IsReadOnly())
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
	// The representation the header and the footer are drawn in — Auto shows both, the title and the
	// picture, whichever are set. The caption is always written: it names the column even when only the
	// picture is shown, and the representation says which of the two are.
	ibRepresentation rep = m_propertyRepresentation->GetValueAsEnum();
	if (rep == ibRepresentation::ibRepresentation_Auto)
		rep = ibRepresentation::ibRepresentation_PictureAndText;

	state.SetValue(wxT("Caption"), GetControlTitle());
	state.SetValue(wxT("FooterText"), m_propertyFooterText->GetValueAsTranslateString());
	state.SetValue(wxT("Representation"), static_cast<s32>(rep));
	if (rep != ibRepresentation::ibRepresentation_Text) {
		const wxString header = host->SendPicture(m_propertyHeaderPicture->GetValueAsPictureDesc());
		if (!header.IsEmpty())
			state.SetValue(wxT("HeaderPicture"), header);
		const wxString footer = host->SendPicture(m_propertyFooterPicture->GetValueAsPictureDesc());
		if (!footer.IsEmpty())
			state.SetValue(wxT("FooterPicture"), footer);
	}

	// Its own Visible, a binding, the functional options — the group above says its own (its Visible).
	const bool sourceMissing = m_propertySource->IsEmptyProperty() && m_model_id == wxNOT_FOUND;
	state.SetValue(wxT("Visible"), m_propertyVisible->GetValueAsBoolean() && !sourceMissing && IsAvailable());
	state.SetValue(wxT("ReadOnly"), IsReadOnly());

	// Sortability is gated by the model's Sorting FEATURE (when the flag is turned off, sorting cannot be
	// used), and by a field to sort by. The ACTIVE sort + direction come from L5 — the composer — matched
	// by the column's OWN bound field (GetSourceFieldName, the same string OnColumnClick commits); no model
	// column-id resolution (a reference dot-path field is not a model column id). So a sort set in the
	// settings window shows on the header as surely as one set by a click.
	const ibValueModelTableBox* owner = GetOwner();
	ibValueModel* model = owner != nullptr ? owner->GetTableModel() : nullptr;
	const wxString field = GetSourceFieldName();
	const bool sortable = model != nullptr && !appData->DesignerMode() && !field.IsEmpty()
		&& model->GetFeatures().Has(ibValueModel::Features::Sorting);
	state.SetValue(wxT("Sortable"), sortable);
	if (sortable) {
		const ibDataComposer& composer = model->GetModelComposer();
		for (size_t i = 0; i < composer.SortCount(); ++i) {
			wxString sortField; bool asc = true;
			if (composer.GetSortAt(i, sortField, asc) && sortField == field) {
				const s32 sort = asc ? 1 : -1;
				state.SetValue(wxT("Sort"), sort);
				break;
			}
		}
	}
}

const ibFormatString* ibValueModelTableBoxColumn::GetCellFormat() const
{
	const ibTranslateString& format = GetFormat();
	if (!format.IsEmpty())
		return &ibBackendTypeConfigFactory::GetFormatFromColumn(format, GetTypeDesc());
	const ibValueModelTableBox* owner = GetOwner();
	const ibValueModel* model = owner != nullptr ? owner->GetTableModel() : nullptr;
	const ibValueModel::ibValueModelColumnCollection* columns = model != nullptr ? model->GetColumnCollection() : nullptr;
	return columns != nullptr ? &columns->GetColumnFormat(GetModelColumn()) : nullptr;
}

void ibValueModelTableBoxColumn::UpdateCell(const ibDataViewItem& item, ibDataNode& cell) const
{
	UpdateCell(item, cell, GetCellFormat(), IsReadOnly());
}

void ibValueModelTableBoxColumn::UpdateCell(const ibDataViewItem& item, ibDataNode& cell, const ibFormatString* format,
	bool readOnly) const
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
			cell.SetValue(wxT("TextColour"), attr.GetColour().GetAsString(wxC2S_HTML_SYNTAX));
		if (attr.HasBackgroundColour())
			cell.SetValue(wxT("BackgroundColour"), attr.GetBackgroundColour().GetAsString(wxC2S_HTML_SYNTAX));
		if (attr.GetBold())
			cell.SetValue(wxT("Bold"), true);
		if (attr.GetItalic())
			cell.SetValue(wxT("Italic"), true);
		if (attr.GetStrikethrough())
			cell.SetValue(wxT("Strikethrough"), true);
		if (attr.GetUnderlined())
			cell.SetValue(wxT("Underlined"), true);
		if (attr.GetPointSize() > 0)
			cell.SetValue(wxT("Size"), static_cast<s32>(attr.GetPointSize()));
		if (!attr.GetFaceName().IsEmpty())
			cell.SetValue(wxT("Face"), attr.GetFaceName());
		if (attr.HasAlignment()) {
			const int align = attr.GetAlignment();
			cell.SetValue(wxT("Align"), wxString((align & wxALIGN_RIGHT) ? wxT("Right")
				: (align & wxALIGN_CENTER_HORIZONTAL) ? wxT("Center") : wxT("Left")));
		}
	}

	// The per-cell value resolves THROUGH this column's binding. A dot-path column ("Counterparty.Supplier")
	// or a header column is resolved by the table — first hop via the dumb model, deeper hops walk the
	// reference; a plain column is the model's own. Whether the cell has one at all (a container's non-first
	// columns) the fetch has asked already.
	wxVariant value;
	if (!owner->ResolveCellValue(item, this, value))
		model->GetValue(value, item, modelColumn);

	if (value.IsNull()) {
		cell.SetValue(wxT("Text"), wxString());
		return;
	}

	// A variant no table model made is shown as it is.
	if (value.GetType() != wxT("value")) {
		cell.SetValue(wxT("Text"), value.MakeString());
		return;
	}

	// Everything below is asked of the value itself, through the format of the column.
	const ibValue& cellValue = static_cast<const ibVariantDataValue*>(value.GetData())->GetValue();
	wxString text;
	if (format != nullptr)
		format->Apply(cellValue, text);
	else
		text = cellValue.GetString();
	cell.SetValue(wxT("Text"), text);
	// …and, in a cell the client may edit, the value with its type (ibValue::Serialize) — the field's way. A list's
	// cells, never edited, carry their text alone and stay as light as they were.
	if (!readOnly && model->EditableLine(item, modelColumn) && cellValue.IsTransferable())
		cellValue.Serialize(cell.Child(wxT("Value")));

	// A boolean is drawn as a tick, and whether it has one is the value's, not its text's; a number stands
	// to the right.
	if (cellValue.GetType() == ibValueTypes::TYPE_BOOLEAN)
		cell.SetValue(wxT("Checked"), cellValue.GetBoolean());
	else if (cellValue.GetType() == ibValueTypes::TYPE_NUMBER)
		cell.SetValue(wxT("Number"), true);
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
	// Written into the CURRENT ROW's cell; the client reads it back with the next fetch of that row.
	ibValueModel::ibValueModelReturnLine* currentLine = GetCurrentLine();
	if (currentLine != nullptr)
		currentLine->SetValueByMetaID(GetModelColumn(), varControlVal);

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
	m_propertyPasswordMode->SetNodeValue(node.GetProperty(m_propertyPasswordMode->GetName()));
	m_propertyMultilineMode->SetNodeValue(node.GetProperty(m_propertyMultilineMode->GetName()));
	m_propertyTexteditMode->SetNodeValue(node.GetProperty(m_propertyTexteditMode->GetName()));
	m_propertyFormat->SetNodeValue(node.GetProperty(m_propertyFormat->GetName()));
	m_propertySelectButton->SetNodeValue(node.GetProperty(m_propertySelectButton->GetName()));
	m_propertyOpenButton->SetNodeValue(node.GetProperty(m_propertyOpenButton->GetName()));
	m_propertyClearButton->SetNodeValue(node.GetProperty(m_propertyClearButton->GetName()));
	m_propertyHeaderAlign->SetNodeValue(node.GetProperty(m_propertyHeaderAlign->GetName()));
	m_propertyFooterAlign->SetNodeValue(node.GetProperty(m_propertyFooterAlign->GetName()));
	m_propertyWidth->SetNodeValue(node.GetProperty(m_propertyWidth->GetName()));
	m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));
	m_propertyResizable->SetNodeValue(node.GetProperty(m_propertyResizable->GetName()));
	//m_propertySortable->SetNodeValue(node.GetProperty(m_propertySortable->GetName()));
	m_propertyReorderable->SetNodeValue(node.GetProperty(m_propertyReorderable->GetName()));
	m_propertyChoiceForm->SetNodeValue(node.GetProperty(m_propertyChoiceForm->GetName()));
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));

	//events
	m_eventOnChange->SetNodeValue(node.GetProperty(m_eventOnChange->GetName()));
	m_eventStartChoice->SetNodeValue(node.GetProperty(m_eventStartChoice->GetName()));
	m_eventStartListChoice->SetNodeValue(node.GetProperty(m_eventStartListChoice->GetName()));
	m_eventClearing->SetNodeValue(node.GetProperty(m_eventClearing->GetName()));
	m_eventOpening->SetNodeValue(node.GetProperty(m_eventOpening->GetName()));
	m_eventChoiceProcessing->SetNodeValue(node.GetProperty(m_eventChoiceProcessing->GetName()));
	
	return ibValueControl::ReadData(node);
}

bool ibValueModelTableBoxColumn::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyRepresentation->GetName(), m_propertyRepresentation->GetNodeValue());
	node.SetProperty(m_propertyFooterText->GetName(), m_propertyFooterText->GetNodeValue());
	node.SetProperty(m_propertyHeaderPicture->GetName(), m_propertyHeaderPicture->GetNodeValue());
	node.SetProperty(m_propertyFooterPicture->GetName(), m_propertyFooterPicture->GetNodeValue());
	node.SetProperty(m_propertyPasswordMode->GetName(), m_propertyPasswordMode->GetNodeValue());
	node.SetProperty(m_propertyMultilineMode->GetName(), m_propertyMultilineMode->GetNodeValue());
	node.SetProperty(m_propertyTexteditMode->GetName(), m_propertyTexteditMode->GetNodeValue());
	node.SetProperty(m_propertyFormat->GetName(), m_propertyFormat->GetNodeValue());
	node.SetProperty(m_propertySelectButton->GetName(), m_propertySelectButton->GetNodeValue());
	node.SetProperty(m_propertyOpenButton->GetName(), m_propertyOpenButton->GetNodeValue());
	node.SetProperty(m_propertyClearButton->GetName(), m_propertyClearButton->GetNodeValue());
	node.SetProperty(m_propertyHeaderAlign->GetName(), m_propertyHeaderAlign->GetNodeValue());
	node.SetProperty(m_propertyFooterAlign->GetName(), m_propertyFooterAlign->GetNodeValue());
	node.SetProperty(m_propertyWidth->GetName(), m_propertyWidth->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());
	node.SetProperty(m_propertyResizable->GetName(), m_propertyResizable->GetNodeValue());
	//node.SetProperty(m_propertySortable->GetName(), m_propertySortable->GetNodeValue());
	node.SetProperty(m_propertyReorderable->GetName(), m_propertyReorderable->GetNodeValue());
	node.SetProperty(m_propertyChoiceForm->GetName(), m_propertyChoiceForm->GetNodeValue());
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());

	//events
	node.SetProperty(m_eventOnChange->GetName(), m_eventOnChange->GetNodeValue());
	node.SetProperty(m_eventStartChoice->GetName(), m_eventStartChoice->GetNodeValue());
	node.SetProperty(m_eventStartListChoice->GetName(), m_eventStartListChoice->GetNodeValue());
	node.SetProperty(m_eventClearing->GetName(), m_eventClearing->GetNodeValue());
	node.SetProperty(m_eventOpening->GetName(), m_eventOpening->GetNodeValue());
	node.SetProperty(m_eventChoiceProcessing->GetName(), m_eventChoiceProcessing->GetNodeValue());
	
	return ibValueControl::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(ibValueModelTableBoxColumn, "TableboxColumn", "TableboxColumn", g_controlTableBoxColumnCLSID);