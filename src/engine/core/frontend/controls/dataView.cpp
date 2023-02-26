#include "dataView.h"
#include "core/common/tableInfo.h"

wxDEFINE_EVENT(wxEVT_DATAVIEW_ITEM_START_DELETING, wxDataViewEvent);

wxBEGIN_EVENT_TABLE(wxDataModelViewCtrl::CDataViewFreezeRowsWindow, wxWindow)
EVT_PAINT(wxDataModelViewCtrl::CDataViewFreezeRowsWindow::OnPaint)
wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(wxDataModelViewCtrl, wxDataViewCtrl);

wxBEGIN_EVENT_TABLE(wxDataModelViewCtrl, wxDataViewCtrl)
EVT_CHAR(wxDataModelViewCtrl::OnChar)
EVT_RIGHT_UP(wxDataModelViewCtrl::OnRightUp)
EVT_CHAR(wxDataModelViewCtrl::OnChar)
wxEND_EVENT_TABLE()

bool wxDataModelViewCtrl::AssociateModel(wxDataViewModel* model)
{
	if (model != NULL) {
		IValueModel* new_model = dynamic_cast<IValueModel*>(model);
		if (new_model != NULL) {
			m_genNotitfier = new wxTableModelNotifier(this);
			new_model->AppendNotifier(m_genNotitfier);
		}
	}
	else {
		IValueModel* old_model = dynamic_cast<IValueModel*>(GetModel());
		if (old_model != NULL)
			old_model->RemoveNotifier(m_genNotitfier);
		wxDELETE(m_genNotitfier);
	}

	return wxDataViewCtrl::AssociateModel(model);
}

#include <wx/sizer.h>
#include <wx/dialog.h>
#include <wx/button.h>

#include "frontend/controls/textEditor.h"

#include "frontend/visualView/controls/controlAttribute.h"
#include "frontend/visualView/controls/form.h"

#include "metadata/metadata.h"
#include "metadata/singleClass.h"

bool wxDataModelViewCtrl::ShowFilter(struct filterRow_t& filter)
{
	enum {
		eModelUse = 1,
		eModelName,
		eModelComparison,
		eModelValue
	};

	class wxFilterDialog : public wxDialog {

		class wxDataViewFilterModel : public wxDataViewVirtualListModel {
			filterRow_t m_filter;
		public:

			inline void CopyValue(wxVariant& variant, const CValue& cValue) const {
				variant = cValue.GetString();
			}

			void SetFilter(const filterRow_t& filter) {
				m_filter = filter;
				Reset(filter.m_filters.size());
			};

			filterRow_t& GetFilter() {
				return m_filter;
			};

			wxDataViewFilterModel() :
				wxDataViewVirtualListModel() {
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
					m_filter.m_filters[row].m_filterComparison = (eComparisonType)variant.GetLong();
					return true;
				}
				else if (col == eModelValue) {
					const CValue& selValue = m_filter.m_filters[row].m_filterValue;
					const CValue& newValue = metadata->CreateObject(selValue.GetTypeClass());
					const wxString& strData = variant.GetString();
					if (strData.Length() > 0) {
						std::vector<CValue> foundedObjects;
						if (newValue.FindValue(strData, foundedObjects)) {
							m_filter.m_filters[row].m_filterValue = CValueTypeDescription::AdjustValue(
								m_filter.m_filters[row].m_filterTypeDescription, foundedObjects.at(0)
							);
						}
						else {
							return false;
						}
					}
					else {
						m_filter.m_filters[row].m_filterValue = CValueTypeDescription::AdjustValue(
							m_filter.m_filters[row].m_filterTypeDescription, newValue
						);
					}
					m_filter.m_filters[row].m_filterUse = true;
					return true;
				}
				return false;
			};
		};

		class wxValueViewRenderer : public wxDataViewCustomRenderer,
			public IControlFrame {
			wxFilterDialog* m_filterDialog;
		public:

			void FinishSelecting() {
				if (m_editorCtrl != NULL) {
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

				DoHandleEditingDone(NULL);
			}

			// This renderer can be either activatable or editable, for demonstration
			// purposes. In real programs, you should select whether the user should be
			// able to activate or edit the cell and it doesn't make sense to switch
			// between the two -- but this is just an example, so it doesn't stop us.
			wxValueViewRenderer(wxFilterDialog* filterDialog)
				: wxDataViewCustomRenderer(wxT("string"), wxDATAVIEW_CELL_EDITABLE, wxALIGN_LEFT), m_filterDialog(filterDialog) {
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
				wxDataViewModel* model,
				const wxDataViewItem& item,
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
				wxTextContainerCtrl* textCtrl = new wxTextContainerCtrl;
				textCtrl->SetDVCMode(true);

				textCtrl->SetPasswordMode(false);
				textCtrl->SetMultilineMode(false);
				textCtrl->SetTextEditMode(true);
				textCtrl->SetButtonSelect(true);
				textCtrl->SetButtonClear(true);
				textCtrl->SetButtonOpen(false);

				bool result = textCtrl->Create(dv, wxID_ANY, value,
					labelRect.GetPosition(),
					labelRect.GetSize());

				if (!result)
					return NULL;

				wxDataViewCtrl* parentWnd = dynamic_cast<wxDataViewCtrl*>(dv->GetParent());
				if (parentWnd != NULL) {
					textCtrl->SetBackgroundColour(parentWnd->GetBackgroundColour());
					textCtrl->SetForegroundColour(parentWnd->GetForegroundColour());
					textCtrl->SetFont(parentWnd->GetFont());
				}
				else {
					textCtrl->SetBackgroundColour(dv->GetBackgroundColour());
					textCtrl->SetForegroundColour(dv->GetForegroundColour());
					textCtrl->SetFont(dv->GetFont());
				}

				textCtrl->BindButtonSelect(&wxValueViewRenderer::OnSelectButtonPressed, this);
				textCtrl->BindButtonClear(&wxValueViewRenderer::OnClearButtonPressed, this);

				textCtrl->SetInsertionPointEnd();
				return textCtrl;
			}

			virtual bool GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value) override {
				wxTextContainerCtrl* textCtrl = wxDynamicCast(ctrl, wxTextContainerCtrl);
				if (textCtrl == NULL)
					return false;
				value = textCtrl->GetTextValue();
				return true;
			}

		public:

			virtual bool GetControlValue(CValue& pvarControlVal) const {
				const wxDataViewItem& item = m_filterDialog->m_dataViewFilter->GetSelection();
				if (!item.IsOk())
					return false;
				size_t index = reinterpret_cast<size_t>(item.GetID());
				filterRow_t& filter = m_filterDialog->m_filterModel->GetFilter();
				filterRow_t::filterData_t& filterData = filter.m_filters[index - 1];
				pvarControlVal = filterData.m_filterValue;
				return true;
			}

			virtual bool SetControlValue(const CValue& varValue) const {
				const wxDataViewItem& item = m_filterDialog->m_dataViewFilter->GetSelection();
				if (!item.IsOk())
					return false;
				size_t index = reinterpret_cast<size_t>(item.GetID());
				filterRow_t& filter = m_filterDialog->m_filterModel->GetFilter();
				filterRow_t::filterData_t& filterData = filter.m_filters[index - 1];
				filterData.m_filterValue = varValue;
				return true;
			}

			virtual bool HasQuickChoice() const {
				CValue selValue; GetControlValue(selValue);
				IObjectValueAbstract* so = metadata->GetAvailableObject(selValue.GetTypeClass());
				if (so != NULL && so->GetObjectType() == eObjectType_simple) {
					return true;
				}
				else if (so != NULL && so->GetObjectType() == eObjectType_value_metadata) {
					IMetaTypeObjectValueSingle* meta_so = dynamic_cast<IMetaTypeObjectValueSingle*>(so);
					if (meta_so != NULL) {
						IMetaObjectRecordDataRef* metaObject = dynamic_cast<IMetaObjectRecordDataRef*>(meta_so->GetMetaObject());
						if (metaObject != NULL)
							return metaObject->HasQuickChoice();
					}
				}
				return false;
			}

		private:

			//events
			void OnSelectButtonPressed(wxCommandEvent& event) {
				const wxDataViewItem& item = m_filterDialog->m_dataViewFilter->GetSelection();
				if (!item.IsOk())
					return;
				size_t index = reinterpret_cast<size_t>(item.GetID());
				filterRow_t& filter = m_filterDialog->m_filterModel->GetFilter();
				filterRow_t::filterData_t& filterData = filter.m_filters[index - 1];
				typeDescription_t& typeDescription = filterData.m_filterTypeDescription;
				if (filterData.m_filterValue.GetType() == eValueTypes::TYPE_EMPTY) {
					const CLASS_ID& clsid = ITypeControl::ShowSelectType(metadata,
						filterData.m_filterTypeDescription
					);
					if (clsid != 0 && metadata->IsRegisterObject(clsid)) {
						filterData.m_filterValue = metadata->CreateObject(clsid);
					}
					return;
				}
				const CLASS_ID& clsid = filterData.m_filterValue.GetTypeClass();
				if (!ITypeControl::QuickChoice(this, clsid, GetEditorCtrl())) {
					IMetaTypeObjectValueSingle* singleValue = metadata->GetTypeObject(clsid);
					if (singleValue != NULL) {
						IMetaObject* metaObject = singleValue->GetMetaObject();
						wxASSERT(metaObject);
						eSelectMode selMode = eSelectMode::eSelectMode_Items; IMetaAttributeObject* attribute = NULL; 
						if (metadata->GetMetaObject(attribute, filterData.m_filterModel, metaObject))
							selMode = attribute->GetSelectMode();
						metaObject->ProcessChoice(this, wxNOT_FOUND, selMode);
					}
				}
			}

			void OnClearButtonPressed(wxCommandEvent& event) {
				const wxDataViewItem& item = m_filterDialog->m_dataViewFilter->GetSelection();
				if (!item.IsOk())
					return;
				size_t index = reinterpret_cast<size_t>(item.GetID());
				filterRow_t& filter = m_filterDialog->m_filterModel->GetFilter();
				filterRow_t::filterData_t& filterData = filter.m_filters[index - 1];
				filterData.m_filterValue = CValueTypeDescription::AdjustValue(filterData.m_filterTypeDescription);
				filterData.m_filterUse = true;
				FinishSelecting();
			}

			virtual void ChoiceProcessing(CValue& vSelected) {
				const wxDataViewItem& item = m_filterDialog->m_dataViewFilter->GetSelection();
				if (!item.IsOk())
					return;
				size_t index = reinterpret_cast<size_t>(item.GetID());
				filterRow_t& filter = m_filterDialog->m_filterModel->GetFilter();
				filterRow_t::filterData_t& filterData = filter.m_filters[index - 1];
				filterData.m_filterValue = vSelected;
				filterData.m_filterUse = true;
				FinishSelecting();
			}

		private:
			wxVariant m_valueVariant;
		};

	private:

		wxDataViewCtrl* m_dataViewFilter;
		wxDataViewColumn* m_dataViewColumnUse;
		wxDataViewColumn* m_dataViewColumnName;
		wxDataViewColumn* m_dataViewColumnComparison;
		wxDataViewColumn* m_dataViewColumnValue;
		wxStdDialogButtonSizer* m_sdbSizer;
		wxButton* m_sdbSizerOK;
		wxButton* m_sdbSizerCancel;
		wxDataViewFilterModel* m_filterModel;

	public:

		void SetFilter(const filterRow_t& filter) {
			m_filterModel->SetFilter(filter);
		}

		filterRow_t GetFilter() const {
			if (m_filterModel != NULL)
				return m_filterModel->GetFilter();
			return filterRow_t();
		}

		wxFilterDialog::wxFilterDialog(wxWindow* parent, wxWindowID id,
			const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(600, 250), long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) :
			wxDialog(parent, id, _("Filter"), pos, size, style)
		{
			wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);

			m_dataViewFilter = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);

			m_dataViewFilter->SetBackgroundColour(parent->GetBackgroundColour());
			m_dataViewFilter->SetForegroundColour(parent->GetForegroundColour());

			m_dataViewFilter->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &wxFilterDialog::OnItemActivated, this);

			wxArrayString arr;
			arr.push_back("Equal");
			arr.push_back("Not equal");

			m_dataViewColumnUse = m_dataViewFilter->AppendToggleColumn(_("Use"), eModelUse, wxDATAVIEW_CELL_ACTIVATABLE, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
			m_dataViewColumnName = m_dataViewFilter->AppendTextColumn(_("Name"), eModelName, wxDATAVIEW_CELL_INERT, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);

			m_dataViewColumnComparison = new wxDataViewColumn(_("Comparison"),
				new wxDataViewChoiceByIndexRenderer(arr, wxDATAVIEW_CELL_EDITABLE, wxAlignment::wxALIGN_LEFT),
				eModelComparison,
				wxNOT_FOUND,
				wxAlignment::wxALIGN_LEFT
			);
			m_dataViewFilter->AppendColumn(m_dataViewColumnComparison);

			m_dataViewColumnValue = new wxDataViewColumn(_("Value"),
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

			wxDialog::SetMinSize(size);
			wxDialog::Centre(wxBOTH);
		}

		virtual ~wxFilterDialog() {
			m_dataViewFilter->AssociateModel(NULL);
			delete m_filterModel;
		}

		void OnItemActivated(wxDataViewEvent& event) {
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

#include <wx/headerctrl.h>

wxSize wxDataModelViewCtrl::GetSizeAvailableForScrollTarget(const wxSize& size) {

	wxSize newsize = size;
	if (!HasFlag(wxDV_NO_HEADER) && (GenericGetHeader()))
		newsize.y -= GenericGetHeader()->GetSize().y;
	if (m_freezeRows)
		newsize.y -= m_freezeRows->GetSize().y;
	return newsize;
}

void wxDataModelViewCtrl::OnChar(wxKeyEvent& event)
{
	// prepare the event this key press maps to
	wxScrollWinEvent newEvent;

	newEvent.SetPosition(0);
	newEvent.SetEventObject(m_win);
	newEvent.SetId(m_win->GetId());

	// this is the default, it's changed to wxHORIZONTAL below if needed
	newEvent.SetOrientation(wxVERTICAL);

	// some key events result in scrolling in both horizontal and vertical
	// direction, e.g. Ctrl-{Home,End}, if this flag is true we should generate
	// a second event in horizontal direction in addition to the primary one
	bool sendHorizontalToo = false;

	switch (event.GetKeyCode())
	{
	case WXK_PAGEUP:
		newEvent.SetEventType(wxEVT_SCROLLWIN_PAGEUP);
		break;

	case WXK_PAGEDOWN:
		newEvent.SetEventType(wxEVT_SCROLLWIN_PAGEDOWN);
		break;

	case WXK_HOME:
		newEvent.SetEventType(wxEVT_SCROLLWIN_TOP);
		sendHorizontalToo = event.ControlDown();
		break;

	case WXK_END:
		newEvent.SetEventType(wxEVT_SCROLLWIN_BOTTOM);
		sendHorizontalToo = event.ControlDown();
		break;

	case WXK_LEFT:
		newEvent.SetOrientation(wxHORIZONTAL);
		wxFALLTHROUGH;

	case WXK_UP:
		newEvent.SetEventType(wxEVT_SCROLLWIN_LINEUP);
		break;

	case WXK_RIGHT:
		newEvent.SetOrientation(wxHORIZONTAL);
		wxFALLTHROUGH;

	case WXK_DOWN:
		newEvent.SetEventType(wxEVT_SCROLLWIN_LINEDOWN);
		break;
	}

	m_win->ProcessWindowEvent(newEvent);

	if (sendHorizontalToo) {
		newEvent.SetOrientation(wxHORIZONTAL);
		m_win->ProcessWindowEvent(newEvent);
	}

	event.Skip();
}

void wxDataModelViewCtrl::OnRightUp(wxMouseEvent& event)
{
	wxDataViewColumn* col = NULL; 
	int xpos = 0;
	unsigned int cols = GetColumnCount();
	unsigned int i;
	for (i = 0; i < cols; i++) {
		wxDataViewColumn* c = GetColumnAt(i);
		if (c->IsHidden())
			continue;      // skip it!
		if (event.GetX() < xpos + c->GetWidth()) {
			col = c;
			break;
		}
		xpos += c->GetWidth();
	}

	event.Skip();
}

void wxDataModelViewCtrl::OnSize(wxSizeEvent& event) {

	if (m_freezeRows &&
		m_freezeRows->GetSize().y <= m_freezeRows->GetBestSize().y) {
		m_freezeRows->Refresh();
	}

	event.Skip();
}

#include <wx/dcbuffer.h>

#ifdef __WXMSW__
#include <wx/msw/private.h>
#endif

int wxDataModelViewCtrl::CDataViewFreezeRowsWindow::GetDefaultRowHeight() const
{
	const int SMALL_ICON_HEIGHT = FromDIP(16);

#ifdef __WXMSW__
	// We would like to use the same line height that Explorer uses. This is
	// different from standard ListView control since Vista.
	if (wxGetWinVersion() >= wxWinVersion_Vista)
		return wxMax(SMALL_ICON_HEIGHT, GetCharHeight()) + FromDIP(6);
	else
#endif // __WXMSW__
		return wxMax(SMALL_ICON_HEIGHT, GetCharHeight()) + FromDIP(1);
}

void wxDataModelViewCtrl::CDataViewFreezeRowsWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxAutoBufferedPaintDC dc(this);
	const wxSize size = GetClientSize();
	dc.SetBrush(GetParent()->GetBackgroundColour());
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle(size);

	//if (m_owner->IsEmpty()) {
	//	// No items to draw.
	//	return;
	//}

	// prepare the DC
	GetOwner()->PrepareDC(dc);
	dc.SetFont(GetFont());

	wxRect update = GetUpdateRegion().GetBox();
	GetOwner()->CalcUnscrolledPosition(update.x, update.y, &update.x, &update.y);

	// compute which items needs to be redrawn
	unsigned int item_start = GetLineAt(wxMax(0, update.y));
	unsigned int item_count =
		wxMin((int)(GetLineAt(wxMax(0, update.y + update.height)) - item_start + 1),
			(int)(GetRowCount() - item_start));

	unsigned int item_last = item_start + item_count;

	// compute which columns needs to be redrawn
	unsigned int cols = GetOwner()->GetColumnCount();
	if (!cols) {
		// we assume that we have at least one column below and painting an
		// empty control is unnecessary anyhow
		return;
	}
	unsigned int col_start = 0;
	unsigned int x_start;
	for (x_start = 0; col_start < cols; col_start++) {
		wxDataViewColumn* col = GetOwner()->GetColumnAt(col_start);
		if (col->IsHidden())
			continue;      // skip it!
		unsigned int w = col->GetWidth();
		if (x_start + w >= (unsigned int)update.x)
			break;
		x_start += w;
	}

	unsigned int col_last = col_start;
	unsigned int x_last = x_start;
	for (; col_last < cols; col_last++) {
		wxDataViewColumn* col = GetOwner()->GetColumnAt(col_last);
		if (col->IsHidden())
			continue;      // skip it!
		if (x_last > (unsigned int)update.GetRight())
			break;
		x_last += col->GetWidth();
	}

	// Instead of calling GetLineStart() for each line from the first to the
	// last one, we will compute the starts of the lines as we iterate over
	// them starting from this one, as this is much more efficient when using
	// wxDV_VARIABLE_LINE_HEIGHT (and doesn't really change anything when not
	// using it, so there is no need to use two different approaches).
	const unsigned int first_line_start = GetLineStart(item_start);

	// Draw background of alternate rows specially if required
	if (GetOwner()->HasFlag(wxDV_ROW_LINES)) {
		wxColour altRowColour = GetOwner()->GetAlternateRowColour();
		if (!altRowColour.IsOk()) {
			// Determine the alternate rows colour automatically from the
			// background colour.
			const wxColour bgColour = GetOwner()->GetBackgroundColour();

			// Depending on the background, alternate row color
			// will be 3% more dark or 50% brighter.
			int alpha = bgColour.GetRGB() > 0x808080 ? 97 : 150;
			altRowColour = bgColour.ChangeLightness(alpha);
		}

		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(altRowColour));

		// We only need to draw the visible part, so limit the rectangle to it.
		const int xRect = GetOwner()->CalcUnscrolledPosition(wxPoint(0, 0)).x;
		const int widthRect = size.x;
		unsigned int cur_line_start = first_line_start;
		for (unsigned int item = item_start; item < item_last; item++) {
			const int h = (item);
			if (item % 2) {
				dc.DrawRectangle(xRect, cur_line_start, widthRect, h);
			}
			cur_line_start += h;
		}
	}

	// Draw horizontal rules if required
	if (GetOwner()->HasFlag(wxDV_HORIZ_RULES))
	{
		dc.SetPen(m_penRule);
		dc.SetBrush(*wxTRANSPARENT_BRUSH);

		unsigned int cur_line_start = first_line_start;
		for (unsigned int i = item_start; i <= item_last; i++) {
			const int h = GetLineHeight(i);
			dc.DrawLine(x_start, cur_line_start, x_last, cur_line_start);
			cur_line_start += h;
		}
	}

	// Draw vertical rules if required
	//if (GetOwner()->HasFlag(wxDV_VERT_RULES)) {
	//	
	//	dc.SetPen(m_penRule);
	//	dc.SetBrush(*wxTRANSPARENT_BRUSH);

	//	// NB: Vertical rules are drawn in the last pixel of a column so that
	//	//     they align perfectly with native MSW wxHeaderCtrl as well as for
	//	//     consistency with MSW native list control. There's no vertical
	//	//     rule at the most-left side of the control.

	//	int x = x_start - 1;
	//	int line_last = GetLineStart(item_last);
	//	for (unsigned int i = col_start; i < col_last; i++)
	//	{
	//		wxDataViewColumn* col = GetOwner()->GetColumnAt(i);
	//		if (col->IsHidden())
	//			continue;       // skip it

	//		x += col->GetWidth();

	//		dc.DrawLine(x, first_line_start,
	//			x, line_last);
	//	}
	//}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool wxTableModelNotifier::NotifyDelete(const wxDataViewItem& item)
{
	return SendEvent(wxEVT_DATAVIEW_ITEM_START_DELETING, item);
}

wxDataViewColumn* wxTableModelNotifier::GetCurrentColumn() const
{
	return m_mainWindow->GetCurrentColumn();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void wxTableModelNotifier::StartEditing(const wxDataViewItem& item, unsigned int col) const
{
	if (!item.IsOk())
		return;
	int viewColumn = m_mainWindow->GetModelColumnIndex(col);
	if (viewColumn != wxNOT_FOUND) {
		m_mainWindow->EditItem(item,
			m_mainWindow->GetColumn(viewColumn)
		);
	}
	else if (col == 0) {
		wxDataViewColumn* currentColumn = GetCurrentColumn();
		if (currentColumn != NULL) {
			m_mainWindow->EditItem(item,
				currentColumn
			);
		}
		else if (m_mainWindow->GetColumnCount() > 0) {
			m_mainWindow->EditItem(item,
				m_mainWindow->GetColumnAt(0)
			);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool wxTableModelNotifier::SendEvent(const wxEventType& eventType, const wxDataViewItem& item)
{
	// Send event
	wxDataViewEvent le(eventType, m_mainWindow, GetCurrentColumn(), item);
	return m_mainWindow->ProcessWindowEvent(le);
}

bool wxTableModelNotifier::SendEvent(const wxEventType& eventType, const wxDataViewItem& item,
	wxDataViewColumn* column)
{
	// Send event
	wxDataViewEvent le(eventType, m_mainWindow, column, item);
	return m_mainWindow->ProcessWindowEvent(le);
}

bool wxTableModelNotifier::ShowFilter(filterRow_t& filter)
{
	return m_mainWindow->ShowFilter(filter);
}

void wxTableModelNotifier::Select(const wxDataViewItem& item) const
{
	m_mainWindow->Select(item);
}

wxDataViewItem wxTableModelNotifier::GetSelection() const
{
	return m_mainWindow->GetSelection();
}

int wxTableModelNotifier::GetSelections(wxDataViewItemArray& sel) const
{
	return m_mainWindow->GetSelections(sel);
}
