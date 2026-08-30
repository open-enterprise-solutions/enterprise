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

		// FIRST THE EDITOR DIES, THEN THE FORM IS REFRESHED — the same order as FinishEditing below, and
		// the same reason: RefreshForm raises the script's `refreshDisplay`, and a handler there may
		// rebuild anything it likes, including the window the live editor is parented to.
		ibDataViewCustomRenderer::CancelEditing();

		if (m_tableBoxColumn != nullptr) {
			ibValueForm* valueForm = m_tableBoxColumn->GetOwnerForm();
			if (valueForm != nullptr) valueForm->RefreshForm();
		}
	}

	virtual bool FinishEditing() {

		// 🛑 FIRST THE EDITOR DIES, THEN THE FORM IS REFRESHED. The refresh below used to run HERE, before
		// the base — while the edit control was still alive and its value not yet written. RefreshForm
		// raises the script's `refreshDisplay`, and a handler there may rebuild anything, the very window
		// the editor is parented to included; the next Edit then reached
		// `wxCHECK_MSG(parent, …)` in wxWindow::CreateUsingMSWClass through CreateEditorCtrl with nothing
		// to parent to (dump 2026-08-29, Max's own reading: *"the control dies first, and the refresh
		// fires after"*).
		//
		// The base DESTROYS THE EDIT CONTROL first and only then hands the value on, so everything that
		// rebuilds anything belongs after it — and there is exactly one such place now.
		const bool finished = ibDataViewCustomRenderer::FinishEditing();

		if (m_tableBoxColumn != nullptr) {
			ibValueForm* valueForm = m_tableBoxColumn->GetOwnerForm();
			if (valueForm != nullptr) valueForm->RefreshForm();
		}

		// ⭐⭐ A ROW EDITED OUT OF THE FILTER MUST LEAVE THE LIST, and THIS is the moment to say so. The
		// write itself only REPAINTS the row — ValueChanged is the narrow notify and the model's
		// RowValueChanged bumps the view generation without re-reading — so a cell changed to something the
		// filter no longer passes stayed on screen until something else happened to read again. The simplest
		// true answer is to read again (Max, 2026-08-29).
		//
		// 🛑⭐⭐ AND THERE IS NO CONDITION ON IT. There was one — "only when there is a filter" — and it
		// was wrong twice for the same reason: a FILTER decides whether a row belongs, a GROUPING
		// decides where it belongs, and a SORT decides that too. Each miss looked different from the
		// outside (a row that would not leave; a row that stayed under a heading it had left while its
		// cell already showed the new value — *"it updates strangely"*, Max, 2026-08-30) and each was
		// the same defect: a caller guessing, on the composer's behalf, whether the composer cares.
		//
		// The third condition would have been the sort, and the fourth whatever arranges rows next.
		// So the rule is the one an edit actually justifies — the edit is over, READ AGAIN — and what
		// that means is the composer's to decide (Max: *"I suggest removing this check altogether"*).
		//
		// ⚠ It is not free and it is not expensive: for a tabular section this recomputes an order in
		// memory, and a DB list's grid is read-only here — a real edit there goes through the object
		// form and its own notify.
		if (finished && m_tableBoxColumn != nullptr) {
			if (ibValueModelTableBox* owner = m_tableBoxColumn->GetOwner()) {
				if (ibValueModel* model = owner->GetTableModel())
					model->RefetchAll();
			}
		}

		return finished;
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

		// The vertical flag has to be spelled out. GetEffectiveAlignmentIfKnown() only adds
		// wxALIGN_CENTRE_VERTICAL when the renderer left the alignment at wxDVR_DEFAULT_ALIGNMENT,
		// so setting a bare horizontal flag here (wxALIGN_LEFT is plain 0 — left AND top) opted
		// this renderer out of vertical centring and pinned every value to the top of its row.
		// Columns drawn by a renderer that never calls SetAlignment stayed centred, which is why
		// the date column looked right next to text that did not.
		if (value.GetType() == wxT("number"))
			SetAlignment(wxALIGN_RIGHT | wxALIGN_CENTRE_VERTICAL);
		else
			SetAlignment(wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL);

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

// ----------------------------------------------------------------------------
// ibDataViewColumnGroupObject
// ----------------------------------------------------------------------------

// The runtime side of a column GROUP, exactly as ibDataViewColumnObject is the
// runtime side of a column: the thing the grid holds, carrying a way back to the
// control that owns it. It draws no cells — it is a header with an orientation, and
// what it TAKES IN is columns (and other groups).
class ibDataViewColumnGroupObject : public ibDataViewColumnGroup {
public:

	ibDataViewColumnGroupObject(ibValueModelTableBoxColumnGroup* control,
		const wxString& title = wxEmptyString,
		ibColumnGroupKind kind = ibColumnGroupVertical,
		wxAlignment align = wxALIGN_CENTER)
		: ibDataViewColumnGroup(title, kind, align), m_tableBoxColumnGroup(control)
	{
	}

	void SetControl(ibValueModelTableBoxColumnGroup* control) { m_tableBoxColumnGroup = control; }
	ibValueModelTableBoxColumnGroup* GetControl() const { return m_tableBoxColumnGroup; }

	// Take a member in — the column's own group pointer is the membership.
	void AppendColumn(ibDataViewColumnObject* column) {
		ibDataViewColumnGroup::AppendColumn(column);
	}

private:

	ibValueModelTableBoxColumnGroup* m_tableBoxColumnGroup;
};

#endif // !_DVC_H__
