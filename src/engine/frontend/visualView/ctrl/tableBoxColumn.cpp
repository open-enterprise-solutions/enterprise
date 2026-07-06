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
#include "frontend/web/webWindow.h"
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
	ibValueModel* modelValue = owner != nullptr ? owner->GetTableModel() : nullptr;
	if (modelValue == nullptr || appData->DesignerMode()
		|| !modelValue->GetFeatures().Has(ibValueModel::Features::Sorting))
		return;
	const wxString field = col->GetSourceFieldName();
	for (size_t i = 0; !field.IsEmpty() && i < modelValue->GetModelComposer().SortCount(); ++i) {
		wxString sortField; bool asc;
		if (modelValue->GetModelComposer().GetSortAt(i, sortField, asc) && sortField == field) {
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

bool ibValueModelTableBoxColumn::GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const
{
	// #3 — the table-COLUMN source list = the current table (what the parent tablebox offers: the
	// tabular sections; the picker roots the bound one and lists its columns) PLUS the form's object
	// attributes (exactly what the ATTRIBUTE picker returns) — so a column can also bind a value from
	// the object ABOVE the table (its header), living alongside the table's own columns (Mode 2).
	bool ok = GetOwner()->GetSourceList(out);                       // current table
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
	return new ibWebStubControl(wxT("tableboxcolumn"));
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
	// Pull the real dataview from the COMPOSITE owner (unwraps the chrome
	// wrapper) — the raw wxparent is the ibChromeWindow, not the dataview.
	ibDataViewCtrl* dataViewCtrl = dynamic_cast<ibDataViewCtrl*>(GetOwner()->GetInnerWx());
	// Composite inner may be gone during host/form teardown (GetInnerWx resolves
	// through form→visualDoc→view→host, which is being destroyed) or not yet
	// built — skip gracefully instead of asserting.
	if (dataViewCtrl == nullptr)
		return;
	ibDataViewColumnObject* dataViewColumn = dynamic_cast<ibDataViewColumnObject*>(wxobject);
	if (dataViewColumn == nullptr)
		return;

	dataViewCtrl->AppendColumn(dataViewColumn);
	GetOwner()->SetCalculateColumnPos();
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

	ibValueModel* modelValue = GetOwner()->GetTableModel();

	// Sortability is gated by the model's Sorting FEATURE (when the flag is turned off, sorting
	// cannot be used). The ACTIVE sort + direction come from L5 (the composer, read via GetSortAt below).
	const bool sortable = modelValue != nullptr && !appData->DesignerMode()
		&& modelValue->GetFeatures().Has(ibValueModel::Features::Sorting);

	// A source-less column is not shown — no binding PATH (m_propertySource) and no explicit model column
	// (m_model_id). It appears once the user binds a source (a field, or a dotted path), mirroring the
	// unbound-source-control visibility gate. It stays selectable via the object tree while hidden, so its
	// Source picker is reachable. (GetModelColumn can't gate this — it falls back to the control id.)
	const bool sourceMissing = m_propertySource->IsEmptyProperty() && m_model_id == wxNOT_FOUND;
	dataViewColumn->SetHidden(!m_propertyVisible->GetValueAsBoolean() || sourceMissing);
	dataViewColumn->SetSortable(sortable);
	dataViewColumn->SetResizeable(m_propertyResizable->GetValueAsBoolean());

	// Reflect the composer's active sort onto THIS column's header arrow (shared with the post-refresh sync).
	dataViewColumn->SyncSortArrowFromModel();

	dataViewColumn->SetColumnModel(source_column);
#endif
}

void ibValueModelTableBoxColumn::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	ibDataViewColumnObject* dataViewColumn = dynamic_cast<ibDataViewColumnObject*>(obj);
	if (dataViewColumn == nullptr)
		return;
	// Take the OWNING dataview straight from the column (SetOwner at AppendColumn)
	// — reliable during teardown, unlike GetOwner()->GetInnerWx() which resolves
	// through the dying host and returns null. Must DETACH the column from the
	// dataview's m_cols here; otherwise its dtor (DoClearColumns) double-frees it
	// after ClearControl's wxDELETE (crash: DoClearColumns over freed memory).
	ibDataViewCtrl* dataViewCtrl = dataViewColumn->GetOwner();
	if (dataViewCtrl == nullptr)
		return;

	dataViewCtrl->DeleteColumn(dataViewColumn);
	if (ibValueModelTableBox* owner = GetOwner())
		owner->SetCalculateColumnPos();
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
	m_propertyTitle->ReadNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyRepresentation->ReadNodeValue(node.GetProperty(m_propertyRepresentation->GetName()));
	m_propertyFooterText->ReadNodeValue(node.GetProperty(m_propertyFooterText->GetName()));
	m_propertyHeaderPicture->ReadNodeValue(node.GetProperty(m_propertyHeaderPicture->GetName()));
	m_propertyFooterPicture->ReadNodeValue(node.GetProperty(m_propertyFooterPicture->GetName()));
	m_propertyPasswordMode->ReadNodeValue(node.GetProperty(m_propertyPasswordMode->GetName()));
	m_propertyMultilineMode->ReadNodeValue(node.GetProperty(m_propertyMultilineMode->GetName()));
	m_propertyTexteditMode->ReadNodeValue(node.GetProperty(m_propertyTexteditMode->GetName()));
	m_propertySelectButton->ReadNodeValue(node.GetProperty(m_propertySelectButton->GetName()));
	m_propertyOpenButton->ReadNodeValue(node.GetProperty(m_propertyOpenButton->GetName()));
	m_propertyClearButton->ReadNodeValue(node.GetProperty(m_propertyClearButton->GetName()));
	m_propertyHeaderAlign->ReadNodeValue(node.GetProperty(m_propertyHeaderAlign->GetName()));
	m_propertyFooterAlign->ReadNodeValue(node.GetProperty(m_propertyFooterAlign->GetName()));
	m_propertyWidth->ReadNodeValue(node.GetProperty(m_propertyWidth->GetName()));
	m_propertyVisible->ReadNodeValue(node.GetProperty(m_propertyVisible->GetName()));
	m_propertyResizable->ReadNodeValue(node.GetProperty(m_propertyResizable->GetName()));
	//m_propertySortable->ReadNodeValue(node.GetProperty(m_propertySortable->GetName()));
	m_propertyReorderable->ReadNodeValue(node.GetProperty(m_propertyReorderable->GetName()));
	m_propertyChoiceForm->ReadNodeValue(node.GetProperty(m_propertyChoiceForm->GetName()));
	m_propertySource->ReadNodeValue(node.GetProperty(m_propertySource->GetName()));

	//events
	m_eventOnChange->ReadNodeValue(node.GetProperty(m_eventOnChange->GetName()));
	m_eventStartChoice->ReadNodeValue(node.GetProperty(m_eventStartChoice->GetName()));
	m_eventStartListChoice->ReadNodeValue(node.GetProperty(m_eventStartListChoice->GetName()));
	m_eventClearing->ReadNodeValue(node.GetProperty(m_eventClearing->GetName()));
	m_eventOpening->ReadNodeValue(node.GetProperty(m_eventOpening->GetName()));
	m_eventChoiceProcessing->ReadNodeValue(node.GetProperty(m_eventChoiceProcessing->GetName()));
	
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
void ibValueModelTableBoxColumn::OnPropertyRefresh(
	class wxPropertyGridManager* /*pg*/, class wxPGProperty* /*pgProperty*/,
	ibProperty* /*property*/) {}
bool ibValueModelTableBoxColumn::OnPropertyChanging(ibProperty* /*property*/,
	const wxVariant& /*newValue*/) { return true; }
void ibValueModelTableBoxColumn::ChoiceProcessing(ibValue& /*vSelected*/) {}
#endif // OES_USE_WEB

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(ibValueModelTableBoxColumn, "TableboxColumn", "TableboxColumn", g_controlTableBoxColumnCLSID);