#include "tableBox.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "form.h"
#ifndef OES_USE_WEB
// Renderer pulls in dataview.h (wxDataView heavy). Web stubs don't
// touch it.
#include "tableBoxColumnRenderer.h"
#endif

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************


#ifdef OES_USE_WEB
#include "frontend/web/webTableBox.h"
#endif

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

#ifndef OES_USE_WEB
// Re-apply the header sort arrow from the composer's ACTIVE sort — match the column's OWN bound field
// (GetSourceFieldName, off m_propertySource, the same string OnColumnClick commits) against the composer's
// sorts; no model column-id resolution (a reference dot-path field is not a model column id). Called from
// OnUpdated (form build) AND from the control's post-refresh sync (a settings-dialog sort reaches the columns
// only here, not through a header click). (ibDataViewColumnObject is a desktop-only type — web has no wxDVC.)
void ibDataViewColumnObject::SyncSortArrowFromModel()
{
	ibValueModelTableBoxColumn* col = GetControl();
	if (col == nullptr) return;
	ibValueModelTableBox* owner = col->GetOwner();
	ibValueModel* model = owner != nullptr ? owner->GetTableModel() : nullptr;
	if (model == nullptr || appData->DesignerMode()
		|| !model->GetFeatures().Has(ibValueModel::Features::Sorting))
		return;
	const wxString field = col->GetSourceFieldName();
	for (size_t i = 0; !field.IsEmpty() && i < model->GetModelComposer().SortCount(); ++i) {
		wxString sortField; bool asc;
		if (model->GetModelComposer().GetSortAt(i, sortField, asc) && sortField == field) {
			SetSortOrder(asc);
			break;
		}
	}
}
#endif

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
	// The parent tablebox may be transiently DETACHED mid-rebuild — a deferred inspector Create can query a
	// just-orphaned column (GetOwner() == null). Skip it gracefully instead of dereferencing null, same as
	// OnCreated below guards the transiently-gone composite inner. The header object still contributes.
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

wxObject* ibValueModelTableBoxColumn::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxparent; (void)visualHost;
	return new ibWebTableBoxColumn(GetControlID());
#else
	ibDataViewColumnObject* dataViewColumn = new ibDataViewColumnObject(this, wxT(""),
		wxNOT_FOUND, wxDVC_DEFAULT_WIDTH, wxALIGN_CENTER, wxDATAVIEW_COL_REORDERABLE);

	dataViewColumn->SetControl(this);
	return dataViewColumn;
#endif
}

void ibValueModelTableBoxColumn::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
#ifndef OES_USE_WEB
	ibDataViewColumnObject* dataViewColumn = dynamic_cast<ibDataViewColumnObject*>(wxobject);
	if (dataViewColumn == nullptr)
		return;

	// HUNG ON ITS HOLDER — the group the parent stands for. Null while the grid is not
	// built yet or already gone (teardown), which this always skipped over.
	if (ibDataViewColumnGroup* holder = ibFindColumnHolder(m_parent))
		holder->AppendColumn(dataViewColumn);
#endif
}

#include "backend/appData.h"

void ibValueModelTableBoxColumn::OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	// OnUpdated only touches the column itself (wxobject) — no need to resolve
	// the parent dataview through the teardown-fragile composite owner.
	ibDataViewColumnObject* dataViewColumn = dynamic_cast<ibDataViewColumnObject*>(wxobject);
	if (dataViewColumn == nullptr)
		return;

	if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Auto) {
		dataViewColumn->SetTitle(GetControlTitle());
		dataViewColumn->SetBitmap(m_propertyHeaderPicture->GetValueAsBitmap());
		dataViewColumn->SetFooterTitle(m_propertyFooterText->GetValueAsTranslateString());
		dataViewColumn->SetFooterBitmap(m_propertyFooterPicture->GetValueAsBitmap());
	}
	else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_PictureAndText) {
		dataViewColumn->SetTitle(GetControlTitle());
		dataViewColumn->SetBitmap(m_propertyHeaderPicture->GetValueAsBitmap());
		dataViewColumn->SetFooterTitle(m_propertyFooterText->GetValueAsTranslateString());
		dataViewColumn->SetFooterBitmap(m_propertyFooterPicture->GetValueAsBitmap());
	}
	else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Picture) {
		dataViewColumn->SetTitle(wxEmptyString);
		dataViewColumn->SetBitmap(m_propertyHeaderPicture->GetValueAsBitmap());
		dataViewColumn->SetFooterTitle(wxEmptyString);
		dataViewColumn->SetFooterBitmap(m_propertyFooterPicture->GetValueAsBitmap());
	}
	else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Text) {
		dataViewColumn->SetTitle(GetControlTitle());
		dataViewColumn->SetBitmap(wxNullBitmap);
		dataViewColumn->SetFooterTitle(m_propertyFooterText->GetValueAsTranslateString());
		dataViewColumn->SetFooterBitmap(wxNullBitmap);
	}

	dataViewColumn->SetWidth(m_propertyWidth->GetValueAsUInteger());
	dataViewColumn->SetAlignment(m_propertyHeaderAlign->GetValueAsEnum());
	dataViewColumn->SetFooterAlignment(m_propertyFooterAlign->GetValueAsEnum());

	const ibFormID source_column = GetModelColumn();

	ibValueModel* model = GetOwner()->GetTableModel();

	// Sortability is gated by the model's Sorting FEATURE (when the flag is turned off, sorting
	// cannot be used). The ACTIVE sort + direction come from L5 (the composer, read via GetSortAt below).
	const bool sortable = model != nullptr && !appData->DesignerMode()
		&& model->GetFeatures().Has(ibValueModel::Features::Sorting);

	// A source-less column is not shown — no binding PATH (m_propertySource) and no explicit model column
	// (m_model_id). It appears once the user binds a source (a field, or a dotted path), mirroring the
	// unbound-source-control visibility gate. It stays selectable via the object tree while hidden, so its
	// Source picker is reachable. (GetModelColumn can't gate this — it falls back to the control id.)
	const bool sourceMissing = m_propertySource->IsEmptyProperty() && m_model_id == wxNOT_FOUND;
	dataViewColumn->SetHidden(!m_propertyVisible->GetValueAsBoolean() || sourceMissing);
	dataViewColumn->SetSortable(sortable);
	dataViewColumn->SetResizeable(m_propertyResizable->GetValueAsBoolean());
	// 🛑 …AND ITS NEIGHBOUR WAS NEVER APPLIED. `Reorderable` serialised both ways and was edited in
	// the designer, while the flag came from `wxDATAVIEW_COL_REORDERABLE` hardcoded at construction —
	// so turning it off did nothing at all, and the two properties sitting side by side in the same
	// category made that impossible to notice (audit, 2026-08-24).
	dataViewColumn->SetReorderable(m_propertyReorderable->GetValueAsBoolean());

	// Reflect the composer's active sort onto THIS column's header arrow (shared with the post-refresh sync).
	dataViewColumn->SyncSortArrowFromModel();

	dataViewColumn->SetColumnModel(source_column);
#else
	// The web shim gets the same answers this method just gave the
	// desktop column, in the browser's spelling. Alignment is TWO
	// answers, as it is on desktop: the header takes the property, the
	// cells take the value type — a number reads right-aligned next to
	// text that does not (the desktop renderer decides it per cell, in
	// CheckedGetValue).
	ibWebTableBoxColumn* webColumn = static_cast<ibWebTableBoxColumn*>(wxobject);

	const bool sourceMissing = m_propertySource->IsEmptyProperty() && m_model_id == wxNOT_FOUND;
	const wxString valueType = ibWebValueTypeName(GetTypeDesc());

	webColumn->SetCaption(GetControlTitle());
	webColumn->SetFieldKey(ibWebTableBoxColumnKey(GetControlID()));
	webColumn->SetWidth(m_propertyWidth->GetValueAsUInteger());
	webColumn->SetHeaderAlign(ibWebAlignName(m_propertyHeaderAlign->GetValueAsEnum()));
	webColumn->SetAlign(valueType == wxT("number") ? wxT("right") : wxT("left"));
	webColumn->SetValueType(valueType);
	webColumn->SetVisibleColumn(m_propertyVisible->GetValueAsBoolean() && !sourceMissing);
	webColumn->SetResizable(m_propertyResizable->GetValueAsBoolean());

	// Sortability is the model's feature, as on the desktop; the direction
	// is read off the composer, matching this column's own bound field —
	// the same pair SyncSortArrowFromModel reads there.
	ibValueModelTableBox* const owner = GetOwner();
	ibValueModel* const model = owner != nullptr ? owner->GetTableModel() : nullptr;
	const wxString field = GetSourceFieldName();
	const bool sortable = model != nullptr && !field.IsEmpty()
		&& model->GetFeatures().Has(ibValueModel::Features::Sorting);
	webColumn->SetSortable(sortable);

	wxString order = wxT("none");
	for (size_t i = 0; sortable && i < model->GetModelComposer().SortCount(); ++i) {
		wxString sortField; bool ascending = true;
		if (model->GetModelComposer().GetSortAt(i, sortField, ascending) && sortField == field) {
			order = ascending ? wxT("asc") : wxT("desc");
			break;
		}
	}
	webColumn->SetSortOrder(order);

	// Whether this column carries an editor. Three answers, all of them the
	// desktop's: the MODEL says whether the column is edited in place at all
	// (a list is not, a tabular section is except its line number); the TABLE
	// says whether the column reads through something other than this row —
	// a dot-path or a foreign root, both read-only there too; and the column's
	// own TextEdit property says whether it is typed into.
	const bool editable = model != nullptr
		&& model->EditableColumn(GetModelColumn())
		&& owner != nullptr && !owner->IsPathColumn(this) && !owner->IsForeignColumn(this)
		&& GetTextEditMode();
	webColumn->SetReadOnly(!editable);

	// The three buttons the cell's editor carries — the column's own properties,
	// the same three the desktop renderer reads onto its inline editor.
	//
	// Select and Open are narrowed to a REFERENCE, and the narrowing is about
	// what exists rather than about what is declared. Both properties default to
	// true on every column, and the desktop can honour that on a number too: its
	// Select opens the quick-choice popup and its Open shows the value. Neither
	// has a web road, and a button that does nothing when pressed is worse than
	// one that is not there — the same reason the table's view-state band is
	// absent here rather than present and dead. Clear needs no road: an empty
	// value of the type the cell holds is a value.
	const bool referenceCell = valueType == wxT("reference");
	webColumn->SetShowSelectButton(GetSelectButton() && referenceCell);
	webColumn->SetShowOpenButton(GetOpenButton() && referenceCell);
	webColumn->SetShowClearButton(GetClearButton());
#endif
}

void ibValueModelTableBoxColumn::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	ibDataViewColumnObject* dataViewColumn = dynamic_cast<ibDataViewColumnObject*>(obj);
	if (dataViewColumn == nullptr)
		return;

	// THE GROUP THAT HOLDS IT LETS IT GO — that is the whole of removing a column, and
	// it MUST happen: being a member is what makes the table's destructor free it, so a
	// column left hanging is freed a second time after the visual host's wxDELETE here
	// (that crash was real). The holder is taken off the column itself — during teardown
	// the walk through the dying host resolves to nothing.
	if (ibDataViewColumnGroup* holder = dataViewColumn->GetParent())
		holder->RemoveColumn(dataViewColumn);
#endif
}

bool ibValueModelTableBoxColumn::CanDeleteControl() const
{
	return m_parent->GetChildCount() > 1;
}

#include "backend/metaCollection/partial/commonObject.h"

//*******************************************************************
//*							 Control value	                        *
//*******************************************************************

#ifndef OES_USE_WEB
#include "frontend/win/ctrls/controlTextEditor.h"
#endif

bool ibValueModelTableBoxColumn::SetControlValue(const ibValue& varControlVal)
{
	ibValueModel::ibValueModelReturnLine* currentLine = GetCurrentLine();
	if (currentLine != nullptr) {
		currentLine->SetValueByMetaID(
			GetModelColumn(), varControlVal
		);
	}

#ifndef OES_USE_WEB
	ibDataViewColumnObject* dataViewColumn =
		dynamic_cast<ibDataViewColumnObject*>(GetWxObject());

	if (dataViewColumn != nullptr) {
		ibDataViewValueRenderer* renderer = dataViewColumn->GetRenderer();
		wxASSERT(renderer);
		ibControlTextEditor* textEditor = dynamic_cast<ibControlTextEditor*>(renderer->GetEditorCtrl());
		if (textEditor != nullptr) {
			textEditor->SetValue(varControlVal.GetString());
			textEditor->SetInsertionPointEnd();
		}
		else {
			renderer->FinishSelecting();
		}
	}
#endif

	m_formOwner->RefreshForm();
	return true;
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

#ifdef OES_USE_WEB
// Methods declared in tableBox.h but normally implemented in the
// auxiliary tableBoxColumn*.cpp files (Event/Property/Renderer) — those
// aren't compiled on web, so provide no-op stubs here so the linker
// finds them. ChoiceProcessing in particular is pure-virtual in the
// base, the override is required even on web.
void ibValueModelTableBoxColumn::OnPropertyCreated(ibProperty* /*property*/) {}
void ibValueModelTableBoxColumn::OnPropertyRefresh() {}
bool ibValueModelTableBoxColumn::OnPropertyChanging(ibProperty* /*property*/,
	const wxVariant& /*newValue*/) { return true; }
// A PICKER CAME BACK. The desktop body (tableBoxColumnEvent.cpp) writes the row
// and then puts the text into the open inline editor; there is no such editor
// here — the browser's is gone by the time this runs, and the form tree that
// goes back carries the new cell. What is left is the part that is not the
// widget, and it is the same: the script may take the choice over, then the
// value goes onto the current line, then OnChange.
//
// Not through SetControlValue: the choice machinery refreshes the owner form
// itself (ibValueForm::ChoiceDocForm), which is what the desktop relies on too.
void ibValueModelTableBoxColumn::ChoiceProcessing(ibValue& vSelected)
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventChoiceProcessing, GetValue(), vSelected, standartProcessing);
	if (!standartProcessing.GetBoolean())
		return;

	if (ibValueModel::ibValueModelReturnLine* currentLine = GetCurrentLine())
		currentLine->SetValueByMetaID(GetModelColumn(), vSelected);

	ibValueControl::CallAsEvent(m_eventOnChange, GetValue());
}

// The three buttons a cell's editor carries, one method each. Same bodies as the
// desktop's OnSelect / OnOpen / OnClearButtonPressed minus the editor widget they
// reach for: which column asked is the request's address, and the row is the one
// the table is standing on.
bool ibValueModelTableBoxColumn::WebCellChoose()
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventStartChoice, GetValue(), standartProcessing);
	if (!standartProcessing.GetBoolean())
		return true;   // a script took the choice over — and said so

	// THE ONE ROUTE, the same one a form field walks: settle the type, then choose
	// a value of it. The form the author named in the property grid, or the
	// metaobject's own.
	const ibMetaID& formId = m_propertyChoiceForm->GetValueAsInteger();
	const ibMetaData* metaData = GetMetaData();
	const ibValueMetaObject* choiceForm = (formId != wxNOT_FOUND && metaData != nullptr)
		? metaData->FindAnyObjectByFilter(formId) : nullptr;
	return ibTypeControlFactory::ChooseValue(this, choiceForm, nullptr);
}

bool ibValueModelTableBoxColumn::WebCellOpen()
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventOpening, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean()) {
		ibValue selValue;
		if (GetControlValue(selValue) && !selValue.IsEmpty())
			selValue.ShowValue();
	}
	return true;
}

bool ibValueModelTableBoxColumn::WebCellClear()
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventClearing, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean())
		SetControlValue();
	return true;
}

// A cell edited in the browser. The desktop equivalent is TextProcessing, run
// when the inline editor commits, and this is the same three steps: coerce the
// typed string through the type the cell already holds, write it through
// SetControlValue — which is where the current line, the source-object update
// and RefreshForm live — and fire the column's OnChange.
//
// A string that does not parse is REFUSED rather than written: the value stands,
// and the form tree that goes back re-states it, so the browser's cell snaps
// back to what the row actually holds.
bool ibValueModelTableBoxColumn::WebCellChanged(const wxString& text)
{
	const ibMetaData* metaData = GetMetaData();
	if (metaData == nullptr)
		return false;

	ibValue current; GetControlValue(current);
	const ibValue& typed = metaData->CreateObject(current.GetClassType());
	if (typed.GetType() == ibValueTypes::TYPE_EMPTY)
		return false;   // the cell has no settled type — nothing to coerce into

	if (text.IsEmpty()) {
		SetControlValue(typed);   // cleared → the empty value of that type
	}
	else {
		std::vector<ibValue> found;
		if (!typed.FindValue(text, found) || found.empty())
			return false;
		SetControlValue(found.front());
	}

	ibValueControl::CallAsEvent(m_eventOnChange, GetValue());
	return true;
}
#endif // OES_USE_WEB

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(ibValueModelTableBoxColumn, "TableboxColumn", "TableboxColumn", g_controlTableBoxColumnCLSID);