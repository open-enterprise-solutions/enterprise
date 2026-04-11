#include "tableView.h"
#include "backend/tableInfo.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibTableViewCtrl, ibDataViewCtrl);

#include "backend/metadataConfiguration.h"

#include "frontend/win/ctrls/controlTextEditor.h"
#include "frontend/visualView/ctrl/typeControl.h"
#include "frontend/visualView/ctrl/form.h"

#include "backend/objCtor.h"

bool ibTableViewCtrl::ShowFilter(struct ibFilterRow& filter)
{
	enum {
		eModelUse = 1,
		eModelName,
		eModelComparison,
		eModelValue
	};

	class wxFilterDialog : public wxDialog {

		class wxDataViewFilterModel : public ibDataViewVirtualListModel {
			ibFilterRow m_filter;
		public:

			inline void CopyValue(wxVariant& variant, const ibValue& cValue) const {
				variant = cValue.GetString();
			}

			void SetFilter(const ibFilterRow& filter) {
				m_filter = filter;
				Reset(filter.m_filters.size());
			};

			ibFilterRow& GetFilter() {
				return m_filter;
			};

			wxDataViewFilterModel() :
				ibDataViewVirtualListModel() {
			}

			virtual void GetValueByRow(wxVariant& variant,
				unsigned row, unsigned col) const {
				if (col == eModelUse) {
					variant = m_filter.m_filters[row].m_filterUse;
				}
				else if (col == eModelName) {
					variant = m_filter.m_filters[row].m_filterPresentation;
				}
				else if (col == eModelComparison) {
					variant = (long)m_filter.m_filters[row].m_filterComparison;
				}
				else if (col == eModelValue) {
					CopyValue(variant, m_filter.m_filters[row].m_filterValue);
				}
			};

			virtual bool SetValueByRow(const wxVariant& variant,
				unsigned row, unsigned col) {

				if (col == eModelUse) {
					m_filter.m_filters[row].m_filterUse = variant.GetBool();
					return true;
				}
				else if (col == eModelName) {
					m_filter.m_filters[row].m_filterName = variant.GetString();
					return true;
				}
				else if (col == eModelComparison) {
					m_filter.m_filters[row].m_filterComparison = (ibComparisonType)variant.GetLong();
					return true;
				}
				else if (col == eModelValue) {
					const ibValue& selValue = m_filter.m_filters[row].m_filterValue;
					const ibValue& newValue = activeMetaData->CreateObject(selValue.GetClassType());
					const wxString& strData = variant.GetString();
					if (strData.Length() > 0) {
						std::vector<ibValue> listValue;
						if (newValue.FindValue(strData, listValue)) {
							m_filter.m_filters[row].m_filterValue = ibValueTypeDescription::AdjustValue(
								m_filter.m_filters[row].m_filterTypeDescription, listValue.at(0)
							);
						}
						else {
							return false;
						}
					}
					else {
						m_filter.m_filters[row].m_filterValue = ibValueTypeDescription::AdjustValue(
							m_filter.m_filters[row].m_filterTypeDescription, newValue
						);
					}
					m_filter.m_filters[row].m_filterUse = true;
					return true;
				}
				return false;
			};
		};

		class wxValueViewRenderer : public ibDataViewCustomRenderer,
			public ibControlFrame {
			wxFilterDialog* m_filterDialog;
		public:

			void FinishSelecting() {
				if (m_editorCtrl != nullptr) {
					// Remove our event handler first to prevent it from (recursively) calling
					// us again as it would do via a call to FinishEditing() when the editor
					// loses focus when we hide it below.
					wxEvtHandler* const handler = m_editorCtrl->PopEventHandler();

					// Hide the control immediately but don't delete it yet as there could be
					// some pending messages for it.
					m_editorCtrl->Hide();

					wxPendingDelete.Append(handler);
					wxPendingDelete.Append(m_editorCtrl);

					// Ensure that DestroyEditControl() is not called again for this control.
					m_editorCtrl.Release();
				}

				DoHandleEditingDone(nullptr);
			}

			// This renderer can be either activatable or editable, for demonstration
			// purposes. In real programs, you should select whether the user should be
			// able to activate or edit the cell and it doesn't make sense to switch
			// between the two -- but this is just an example, so it doesn't stop us.
			wxValueViewRenderer(wxFilterDialog* filterDialog)
				: ibDataViewCustomRenderer(wxT("string"), wxDATAVIEW_CELL_EDITABLE, wxALIGN_LEFT), m_filterDialog(filterDialog) {
			}

			virtual bool Render(wxRect rect, wxDC* dc, int state) override {
				RenderText(m_valueVariant,
					0, // no offset
					rect,
					dc,
					state);

				return true;
			}

			virtual bool ActivateCell(const wxRect& cell,
				ibDataViewModel* model,
				const ibDataViewItem& item,
				unsigned int col,
				const wxMouseEvent* mouseEvent) override {
				return false;
			}

			virtual wxSize GetSize() const override {
				if (!m_valueVariant.IsNull()) {
					return GetTextExtent(m_valueVariant);
				}
				else {
					return GetView()->FromDIP(wxSize(wxDVC_DEFAULT_RENDERER_SIZE,
						wxDVC_DEFAULT_RENDERER_SIZE));
				}
			}

			virtual bool SetValue(const wxVariant& value) override {
				m_valueVariant = value.GetString();
				return true;
			}

			virtual bool GetValue(wxVariant& WXUNUSED(value)) const override {
				return true;
			}

#if wxUSE_ACCESSIBILITY
			virtual wxString GetAccessibleDescription() const override {
				return m_valueVariant;
			}
#endif // wxUSE_ACCESSIBILITY

			virtual bool HasEditorCtrl() const override {
				return true;
			}

			virtual wxWindow* CreateEditorCtrl(wxWindow* dv,
				wxRect labelRect,
				const wxVariant& value) override {

				ibControlTextEditor* textEditor = new ibControlTextEditor;
				textEditor->SetDVCMode(true);

				// create the window hidden to prevent flicker
				textEditor->Show(false);

				bool result = textEditor->Create(dv, wxID_ANY, value,
					labelRect.GetPosition(),
					labelRect.GetSize());

				if (!result)
					return nullptr;

				textEditor->ShowSelectButton(true);
				textEditor->ShowClearButton(true);
				textEditor->ShowOpenButton(false);

				ibDataViewCtrl* parentWnd = dynamic_cast<ibDataViewCtrl*>(dv->GetParent());
				if (parentWnd != nullptr) {
					textEditor->SetBackgroundColour(parentWnd->GetBackgroundColour());
					textEditor->SetForegroundColour(parentWnd->GetForegroundColour());
					textEditor->SetFont(parentWnd->GetFont());
				}
				else {
					textEditor->SetBackgroundColour(dv->GetBackgroundColour());
					textEditor->SetForegroundColour(dv->GetForegroundColour());
					textEditor->SetFont(dv->GetFont());
				}

				textEditor->SetPasswordMode(false);
				textEditor->SetMultilineMode(false);
				textEditor->SetTextEditMode(true);

				textEditor->Bind(wxEVT_CONTROL_BUTTON_SELECT, &wxValueViewRenderer::OnSelectButtonPressed, this);
				textEditor->Bind(wxEVT_CONTROL_BUTTON_CLEAR, &wxValueViewRenderer::OnClearButtonPressed, this);

				textEditor->LayoutControls();
				textEditor->Show(true);

				textEditor->SetInsertionPointEnd();
				return textEditor;
			}

			virtual bool GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value) override {
				ibControlTextEditor* textEditor = wxDynamicCast(ctrl, ibControlTextEditor);
				if (textEditor == nullptr)
					return false;
				value = textEditor->GetValue();
				return true;
			}

			// Counter reference
			virtual void ControlIncrRef() {}
			virtual void ControlDecrRef() {}

		public:

			virtual bool GetControlValue(ibValue& pvarControlVal) const {
				const ibDataViewItem& item = m_filterDialog->m_dataViewFilter->GetSelection();
				if (!item.IsOk())
					return false;
				size_t index = reinterpret_cast<size_t>(item.GetID());
				ibFilterRow& filter = m_filterDialog->m_filterModel->GetFilter();
				ibFilterRow::ibFilterData& filterData = filter.m_filters[index - 1];
				pvarControlVal = filterData.m_filterValue;
				return true;
			}

			virtual ibGuid GetControlGuid() const {
				const ibDataViewItem& item = m_filterDialog->m_dataViewFilter->GetSelection();
				if (!item.IsOk())
					return wxNullGuid;
				size_t index = reinterpret_cast<size_t>(item.GetID());
				ibFilterRow& filter = m_filterDialog->m_filterModel->GetFilter();
				ibFilterRow::ibFilterData& filterData = filter.m_filters[index - 1];
				return filterData.m_filterGuid;
			}

			virtual bool SetControlValue(const ibValue& varValue) const {
				const ibDataViewItem& item = m_filterDialog->m_dataViewFilter->GetSelection();
				if (!item.IsOk())
					return false;
				size_t index = reinterpret_cast<size_t>(item.GetID());
				ibFilterRow& filter = m_filterDialog->m_filterModel->GetFilter();
				ibFilterRow::ibFilterData& filterData = filter.m_filters[index - 1];
				filterData.m_filterValue = varValue;
				return true;
			}

			virtual bool HasQuickChoice() const {
				ibValue selValue; GetControlValue(selValue);
				const ibCtorAbstractType* so = activeMetaData->GetAvailableCtor(selValue.GetClassType());
				if (so != nullptr && so->GetObjectTypeCtor() == ibCtorObjectType_object_primitive) {
					return true;
				}
				else if (so != nullptr && so->GetObjectTypeCtor() == ibCtorObjectType_object_enum) {
					return true;
				}
				else if (so != nullptr && so->GetObjectTypeCtor() == ibCtorObjectType_object_meta_value) {
					const ibCtorMetaValueType* meta_so = dynamic_cast<const ibCtorMetaValueType*>(so);
					if (meta_so != nullptr) {
						ibValueMetaObjectRecordDataRef* metaObject = dynamic_cast<ibValueMetaObjectRecordDataRef*>(meta_so->GetMetaObject());
						if (metaObject != nullptr)
							return metaObject->HasQuickChoice();
					}
				}
				return false;
			}

		private:

			//events
			void OnSelectButtonPressed(wxCommandEvent& event) {
				const ibDataViewItem& item = m_filterDialog->m_dataViewFilter->GetSelection();
				if (!item.IsOk())
					return;
				size_t index = reinterpret_cast<size_t>(item.GetID());
				ibFilterRow& filter = m_filterDialog->m_filterModel->GetFilter();
				ibFilterRow::ibFilterData& filterData = filter.m_filters[index - 1];
				ibTypeDescription& typeDescription = filterData.m_filterTypeDescription;
				if (filterData.m_filterValue.GetType() == ibValueTypes::TYPE_EMPTY) {
					const ibClassID& clsid = ibTypeControlFactory::ShowSelectType(activeMetaData,
						filterData.m_filterTypeDescription
					);
					if (clsid != 0 && activeMetaData->IsRegisterCtor(clsid)) {
						filterData.m_filterValue = activeMetaData->CreateObject(clsid);
					}
					return;
				}
				const ibClassID& clsid = filterData.m_filterValue.GetClassType();
				if (!ibTypeControlFactory::QuickChoice(this, clsid, GetEditorCtrl())) {
					const ibCtorMetaValueType* singleValue = activeMetaData->GetTypeCtor(clsid);
					if (singleValue != nullptr) {
						ibValueMetaObject* metaObject = singleValue->GetMetaObject();
						wxASSERT(metaObject);
						const ibValueMetaObjectAttributeBase* attribute = activeMetaData->FindAnyObjectByFilter<ibValueMetaObjectAttributeBase>(filterData.m_filterModel, true);

						ibSelectMode selMode = ibSelectMode::ibSelectMode_Items;
						if (attribute != nullptr)
							selMode = attribute->GetSelectMode();
						metaObject->ProcessChoice(this, wxEmptyString, selMode);
					}
				}
			}

			void OnClearButtonPressed(wxCommandEvent& event) {
				const ibDataViewItem& item = m_filterDialog->m_dataViewFilter->GetSelection();
				if (!item.IsOk())
					return;
				size_t index = reinterpret_cast<size_t>(item.GetID());
				ibFilterRow& filter = m_filterDialog->m_filterModel->GetFilter();
				ibFilterRow::ibFilterData& filterData = filter.m_filters[index - 1];
				filterData.m_filterValue = ibValueTypeDescription::AdjustValue(filterData.m_filterTypeDescription);
				filterData.m_filterUse = true;
				FinishSelecting();
			}

			virtual void ChoiceProcessing(ibValue& vSelected) {
				const ibDataViewItem& item = m_filterDialog->m_dataViewFilter->GetSelection();
				if (!item.IsOk())
					return;
				size_t index = reinterpret_cast<size_t>(item.GetID());
				ibFilterRow& filter = m_filterDialog->m_filterModel->GetFilter();
				ibFilterRow::ibFilterData& filterData = filter.m_filters[index - 1];
				filterData.m_filterValue = vSelected;
				filterData.m_filterUse = true;
				FinishSelecting();
			}

		private:
			wxVariant m_valueVariant;
		};

	private:

		ibDataViewCtrl* m_dataViewFilter;
		ibDataViewColumn* m_dataViewColumnUse;
		ibDataViewColumn* m_dataViewColumnName;
		ibDataViewColumn* m_dataViewColumnComparison;
		ibDataViewColumn* m_dataViewColumnValue;
		wxStdDialogButtonSizer* m_sdbSizer;
		wxButton* m_sdbSizerOK;
		wxButton* m_sdbSizerCancel;
		wxDataViewFilterModel* m_filterModel;

	public:

		void SetFilter(const ibFilterRow& filter) {
			m_filterModel->SetFilter(filter);
		}

		ibFilterRow GetFilter() const {
			if (m_filterModel != nullptr)
				return m_filterModel->GetFilter();
			return ibFilterRow();
		}

		wxFilterDialog(wxWindow* parent, wxWindowID id,
			const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(600, 250), long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) :
			wxDialog(parent, id, _("Filter"), pos, size, style)
		{
			wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);

			m_dataViewFilter = new ibDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);

			m_dataViewFilter->SetBackgroundColour(parent->GetBackgroundColour());
			m_dataViewFilter->SetForegroundColour(parent->GetForegroundColour());

			m_dataViewFilter->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &wxFilterDialog::OnItemActivated, this);

			wxArrayString arr;
			arr.push_back(_("Equal"));
			arr.push_back(_("Not equal"));

			m_dataViewColumnUse = m_dataViewFilter->AppendToggleColumn(_("Use"), eModelUse, wxDATAVIEW_CELL_ACTIVATABLE, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
			m_dataViewColumnName = m_dataViewFilter->AppendTextColumn(_("Name"), eModelName, wxDATAVIEW_CELL_INERT, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);

			m_dataViewColumnComparison = new ibDataViewColumn(_("Comparison"),
				new ibDataViewChoiceByIndexRenderer(arr, wxDATAVIEW_CELL_EDITABLE, wxAlignment::wxALIGN_LEFT),
				eModelComparison,
				wxNOT_FOUND,
				wxAlignment::wxALIGN_LEFT
			);
			m_dataViewFilter->AppendColumn(m_dataViewColumnComparison);

			m_dataViewColumnValue = new ibDataViewColumn(_("Value"),
				new wxValueViewRenderer(this),
				eModelValue,
				wxNOT_FOUND,
				wxAlignment::wxALIGN_LEFT
			);
			m_dataViewFilter->AppendColumn(m_dataViewColumnValue);

			m_sdbSizer = new wxStdDialogButtonSizer;
			m_sdbSizerOK = new wxButton(this, wxID_OK);
			m_sdbSizer->AddButton(m_sdbSizerOK);
			m_sdbSizerCancel = new wxButton(this, wxID_CANCEL);
			m_sdbSizer->AddButton(m_sdbSizerCancel);
			m_sdbSizer->Realize();

			wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
			mainSizer->Add(m_dataViewFilter, 1, wxALL | wxEXPAND, 5);
			mainSizer->Add(m_sdbSizer, 0, wxEXPAND, 5);
			wxDialog::SetSizer(mainSizer);
			wxDialog::Layout();
			mainSizer->Fit(this);

			m_filterModel = new wxDataViewFilterModel();
			m_dataViewFilter->AssociateModel(m_filterModel);

			wxIcon dlg_icon;
			dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_picFilterCLSID));

			wxDialog::SetIcon(dlg_icon);

			wxDialog::SetMinSize(size);
			wxDialog::Centre(wxBOTH);
		}

		virtual ~wxFilterDialog() {
			m_dataViewFilter->AssociateModel(nullptr);
			delete m_filterModel;
		}

		void OnItemActivated(ibDataViewEvent& event) {
			m_dataViewFilter->EditItem(event.GetItem(), event.GetDataViewColumn());
			event.Skip();
		}
	};

	wxFilterDialog* dialog =
		new wxFilterDialog(this, wxID_ANY);
	dialog->SetFilter(filter);
	bool result = false;
	if (dialog->ShowModal() == wxID_OK) {
		filter = dialog->GetFilter(); result = true;
	}
	dialog->Destroy();
	return result;
}

bool ibTableViewCtrl::ShowViewMode()
{
	class wxTableViewModeDialog : public wxDialog {

	public:

		wxTableViewModeDialog(wxWindow* parent, wxWindowID id, ibDataViewViewMode mode) :
			wxDialog(parent, id, _("View mode"))
		{
			this->SetSizeHints(wxDefaultSize, wxDefaultSize);

			wxBoxSizer* bSizerMain = new wxBoxSizer(wxVERTICAL);

			wxStaticBoxSizer* sbSizerView = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, _("View")), wxVERTICAL);

			m_radioBtnTree = new wxRadioButton(sbSizerView->GetStaticBox(), wxID_ANY, _("Tree"), wxDefaultPosition, wxDefaultSize, 0);
			if (mode == ibDataViewViewMode::ibDataViewTree) m_radioBtnTree->SetValue(true);
			m_radioBtnHierarchy = new wxRadioButton(sbSizerView->GetStaticBox(), wxID_ANY, _("Hierarchy"), wxDefaultPosition, wxDefaultSize, 0);
			if (mode == ibDataViewViewMode::ibDataViewHierarchical) m_radioBtnHierarchy->SetValue(true);
			m_radioBtnList = new wxRadioButton(sbSizerView->GetStaticBox(), wxID_ANY, _("List"), wxDefaultPosition, wxDefaultSize, 0);
			if (mode == ibDataViewViewMode::ibDataViewList) m_radioBtnList->SetValue(true);

			sbSizerView->Add(m_radioBtnTree, 0, wxALL, 5);
			sbSizerView->Add(m_radioBtnHierarchy, 0, wxALL, 5);
			sbSizerView->Add(m_radioBtnList, 0, wxALL, 5);

			bSizerMain->Add(sbSizerView, 1, wxEXPAND, 5);

			m_sdbSizer = new wxStdDialogButtonSizer();
			m_sdbSizerOK = new wxButton(this, wxID_OK);
			m_sdbSizer->AddButton(m_sdbSizerOK);
			m_sdbSizerCancel = new wxButton(this, wxID_CANCEL);
			m_sdbSizer->AddButton(m_sdbSizerCancel);
			m_sdbSizer->Realize();

			bSizerMain->Add(m_sdbSizer, 0, wxEXPAND, 5);

			this->SetSizer(bSizerMain);
			this->Layout();
			bSizerMain->Fit(this);

			wxIcon dlg_icon;
			dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_picHierarchyCLSID));

			wxDialog::SetIcon(dlg_icon);
			wxDialog::Centre(wxBOTH);
		}

		ibDataViewViewMode GetViewMode() const
		{
			if (m_radioBtnTree->GetValue())
				return ibDataViewViewMode::ibDataViewTree;
			else if (m_radioBtnHierarchy->GetValue())
				return ibDataViewViewMode::ibDataViewHierarchical;
			else if (m_radioBtnList->GetValue())
				return ibDataViewViewMode::ibDataViewList;

			return ibDataViewViewMode::ibDataViewList;
		}

	private:

		wxRadioButton* m_radioBtnTree;
		wxRadioButton* m_radioBtnHierarchy;
		wxRadioButton* m_radioBtnList;
		wxStdDialogButtonSizer* m_sdbSizer;
		wxButton* m_sdbSizerOK;
		wxButton* m_sdbSizerCancel;
	};

	wxTableViewModeDialog* dialog =
		new wxTableViewModeDialog(this, wxID_ANY, GetViewMode());

	bool result = false;
	if (dialog->ShowModal() == wxID_OK) {

		ibTableViewCtrl::SetViewMode(dialog->GetViewMode());
		result = true;
	}
	
	dialog->Destroy();
	return result;
}
