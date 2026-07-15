#ifndef __DVC_H__
#define __DVC_H__

#include "frontend/win/ctrls/dataview/dataview.h"

// ----------------------------------------------------------------------------
// ibDataViewValueRenderer
// ----------------------------------------------------------------------------

#include "frontend/visualView/ctrl/form.h"
#include "frontend/visualView/ctrl/tableBox.h"

class ibDataViewValueRenderer :
	public ibDataViewCustomRenderer {
public:

	virtual void FinishSelecting() {

		if (m_tableBoxColumn != nullptr) {
			ibValueForm* valueForm = m_tableBoxColumn->GetOwnerForm();
			if (valueForm != nullptr) valueForm->RefreshForm();
		}

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

	virtual void CancelEditing() {

		if (m_tableBoxColumn != nullptr) {
			ibValueForm* valueForm = m_tableBoxColumn->GetOwnerForm();
			if (valueForm != nullptr) valueForm->RefreshForm();
		}

		ibDataViewCustomRenderer::CancelEditing();
	}

	virtual bool FinishEditing() {

		if (m_tableBoxColumn != nullptr) {
			ibValueForm* valueForm = m_tableBoxColumn->GetOwnerForm();
			if (valueForm != nullptr) valueForm->RefreshForm();
		}

		return ibDataViewCustomRenderer::FinishEditing();
	}

	// A dot-path OR a foreign-root (header) column is read-only — its value is resolved through the
	// dot / the form, not stored, so it can't be written back. Suppress inline editing (the cell
	// still renders the resolved text).
	virtual bool StartEditing(const ibDataViewItem& item, wxRect labelRect) override {
		if (m_tableBoxColumn != nullptr) {
			ibValueModelTableBox* owner = m_tableBoxColumn->GetOwner();
			if (owner != nullptr && (owner->IsPathColumn(m_tableBoxColumn) || owner->IsForeignColumn(m_tableBoxColumn)))
				return false;
		}
		return ibDataViewCustomRenderer::StartEditing(item, labelRect);
	}

	// This renderer can be either activatable or editable, for demonstration
	// purposes. In real programs, you should select whether the user should be
	// able to activate or edit the cell and it doesn't make sense to switch
	// between the two -- but this is just an example, so it doesn't stop us.
	explicit ibDataViewValueRenderer(ibValueModelTableBoxColumn* tableBoxColumn)
		: ibDataViewCustomRenderer(wxT("string"), wxDATAVIEW_CELL_EDITABLE, wxALIGN_LEFT), m_tableBoxColumn(tableBoxColumn)
	{
	}

	virtual bool IsCompatibleVariantType(const wxString& variantType) const { return true; }

	virtual bool Render(wxRect rect, wxDC* dc, int state) override
	{
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
		const wxMouseEvent* mouseEvent) override
	{
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

		if (value.GetType() == wxT("number"))
			SetAlignment(wxALIGN_RIGHT);
		else
			SetAlignment(wxALIGN_LEFT);

		m_valueVariant = value;
		return true;
	}

	virtual bool GetValue(wxVariant& WXUNUSED(value)) const override
	{
		return true;
	}

	// Fork power: the per-cell value fetch resolves THROUGH this column's binding. A dot-path
	// column ("Counterparty.Supplier") is resolved per row on the front — first hop via the dumb
	// model, deeper hops walk the reference. A plain column falls through to the base (model).
	virtual wxVariant CheckedGetValue(const ibDataViewModel* model,
		const ibDataViewItem& item, unsigned column) const override
	{
		if (m_tableBoxColumn != nullptr) {
			ibValueModelTableBox* owner = m_tableBoxColumn->GetOwner();
			wxVariant resolved;
			if (owner != nullptr && owner->ResolveCellValue(item, m_tableBoxColumn, resolved))
				return resolved;
		}
		return ibDataViewRendererBase::CheckedGetValue(model, item, column);
	}

#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const override { return m_valueVariant; }
#endif // wxUSE_ACCESSIBILITY

	virtual bool HasEditorCtrl() const override {
		return true;
	}

	virtual wxWindow* CreateEditorCtrl(wxWindow* parent,
		wxRect labelRect,
		const wxVariant& value) override;

	virtual bool GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value) override;

private:

	ibValueModelTableBoxColumn* m_tableBoxColumn;
	wxVariant m_valueVariant;
};

// ----------------------------------------------------------------------------
// ibDataViewColumnObject
// ----------------------------------------------------------------------------

class ibDataViewColumnObject :
	public ibDataViewColumn, public wxObject {
public:

	ibDataViewColumnObject(ibValueModelTableBoxColumn* col,
		const wxString& title,
		unsigned int model_column,
		int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE)
		:
		ibDataViewColumn(title, new ibDataViewValueRenderer(col), model_column, width, align, flags)
	{
	}

	ibDataViewColumnObject(ibValueModelTableBoxColumn* col,
		const wxBitmap& bitmap,
		unsigned int model_column,
		int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE)
		:
		ibDataViewColumn(bitmap, new ibDataViewValueRenderer(col), model_column, width, align, flags)
	{
	}

	ibDataViewValueRenderer* GetRenderer() const { return static_cast<ibDataViewValueRenderer*>(m_renderer); }

	void SetControl(ibValueModelTableBoxColumn* control) { m_tableBoxColumn = control; }
	ibValueModelTableBoxColumn* GetControl() const { return m_tableBoxColumn; }

	void SetColumnModel(unsigned int col_model) { m_model_column = col_model; }

	// Re-apply this column's header sort arrow from the composer's active sort (out-of-line — needs the
	// tablebox model + composer). Called by the control on a data refresh so a settings-dialog sort updates
	// the arrow without a full column rebuild.
	void SyncSortArrowFromModel() override;

private:

	ibValueModelTableBoxColumn* m_tableBoxColumn;
};

#endif // !_DVC_H__
