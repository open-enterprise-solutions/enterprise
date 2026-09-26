#ifndef __DVC_H__
#define __DVC_H__

#include "frontend/win/ctrls/dataview/dataview.h"

// ----------------------------------------------------------------------------
// ibDataViewValueRenderer
// ----------------------------------------------------------------------------

#include "frontend/visualView/ctrl/form.h"
#include "frontend/visualView/ctrl/tableBox.h"

#include "backend/formatString.h"   // ibFormatString — what a cell is shown through (GetCellFormat)

#include <wx/renderer.h>   // wxRendererNative::DrawCheckMark — a boolean cell
#include <optional>

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
		// A BOOLEAN IS A TICK — the platform's own, where the text would start, not the word for it; false is an
		// empty cell. A MARK, not a checkbox: a box says "click me", and a list cell is not ticked by a click. Not
		// centred either: in a wide column a centred mark stands away from the row's picture.
		if (m_valueFlag.has_value()) {
			if (*m_valueFlag) {
				wxWindow* const view = GetView();
				const wxSize mark = wxRendererNative::Get().GetCheckMarkSize(view);
				const wxRect at(rect.x, rect.y + (rect.height - mark.y) / 2, mark.x, mark.y);
				wxRendererNative::Get().DrawCheckMark(view, *dc, at);
			}
			return true;
		}

		// ⭐ THE TEXT, NOT THE VARIANT. Passing m_valueVariant here converted it to a string on every
		// draw - and GetSize() below converted the same variant separately to measure it, so one
		// painted cell built the string twice. wxVariant::operator wxString was 3.10% of the whole
		// process on a grid scroll (measured 2026-09-06); it is now built once, in SetValue.
		RenderText(m_valueText,
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
		if (m_valueFlag.has_value())
			return wxRendererNative::Get().GetCheckMarkSize(GetView());
		if (!m_valueVariant.IsNull()) {
			return GetTextExtent(m_valueText);
		}
		else {
			return GetView()->FromDIP(wxSize(wxDVC_DEFAULT_RENDERER_SIZE,
				wxDVC_DEFAULT_RENDERER_SIZE));
		}
	}

	virtual bool SetValue(const wxVariant& value) override {

		m_valueVariant = value;
		m_valueText.clear();
		m_valueFlag.reset();

		// The vertical flag has to be spelled out. GetEffectiveAlignmentIfKnown() only adds
		// wxALIGN_CENTRE_VERTICAL when the renderer left the alignment at wxDVR_DEFAULT_ALIGNMENT,
		// so setting a bare horizontal flag here (wxALIGN_LEFT is plain 0 — left AND top) opted
		// this renderer out of vertical centring and pinned every value to the top of its row.
		// Columns drawn by a renderer that never calls SetAlignment stayed centred, which is why
		// the date column looked right next to text that did not.
		if (value.IsNull()) {
			SetAlignment(wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL);
			return true;
		}

		// A variant no table model made — a settings dialog's own text — is shown as it is.
		const ibValue* cell = GetCellValue(value);
		if (cell == nullptr) {
			SetAlignment(wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL);
			m_valueText = value.MakeString();
			return true;
		}

		// Everything below is asked of the value itself.
		SetAlignment((cell->GetType() == ibValueTypes::TYPE_NUMBER ? wxALIGN_RIGHT : wxALIGN_LEFT) | wxALIGN_CENTRE_VERTICAL);

		// Built ONCE, here, where the value arrives - drawing and measuring both read it, through the format
		// of the column: found once for a paint pass over the column (StartColumn), by the cell outside one.
		const ibFormatString* format = !m_inColumn ? GetCellFormat()
			: m_columnFormat.has_value() ? &*m_columnFormat : nullptr;
		if (format != nullptr)
			format->Apply(*cell, m_valueText);
		else
			m_valueText = cell->GetString();

		// A boolean is drawn as a tick (Render), and whether it has one is the value's, not its text's.
		if (cell->GetType() == ibValueTypes::TYPE_BOOLEAN)
			m_valueFlag = cell->GetBoolean();
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

	// THE VALUE A CELL HOLDS — null when the variant is not one a table model made. The settings dialogs (the
	// filter, the composer, a row's value) draw their own wxVariant text with this renderer too, and a cast
	// taken on trust called wxVariantDataString through this class's table (dump 2026-09-25, the filter).
	static const ibValue* GetCellValue(const wxVariant& value) {
		if (value.GetType() != wxT("value"))
			return nullptr;
		return &static_cast<const ibVariantDataValue*>(value.GetData())->GetValue();
	}

	// THE FORMAT THE COLUMN SHOWS ITS CELLS WITH — its own where it has one, else the one its model column
	// gives (the attribute's, else its type's). Null while the column stands in no table model.
	const ibFormatString* GetCellFormat() const {
		if (m_tableBoxColumn == nullptr)
			return nullptr;
		const ibTranslateString& format = m_tableBoxColumn->GetFormat();
		if (!format.IsEmpty())
			return &ibBackendTypeConfigFactory::GetFormatFromColumn(format, m_tableBoxColumn->GetTypeDesc());
		ibValueModelTableBox* owner = m_tableBoxColumn->GetOwner();
		ibValueModel* model = owner != nullptr ? owner->GetTableModel() : nullptr;
		const ibValueModel::ibValueModelColumnCollection* columns = model != nullptr ? model->GetColumnCollection() : nullptr;
		return columns != nullptr ? &columns->GetColumnFormat(m_tableBoxColumn->GetModelColumn()) : nullptr;
	}

	// …FOUND ONCE FOR A PAINT PASS OVER THE COLUMN, and kept — every cell of the column in the pass is written
	// through it. Finding it per cell was a third of preparing a value (13 us of 38 per cell, Debug, paint probe
	// 2026-09-26): the column looked up by id, its type copied, its format's text read.
	virtual void StartColumn(const ibDataViewModel* WXUNUSED(model), unsigned WXUNUSED(column)) override {
		const ibFormatString* format = GetCellFormat();
		m_columnFormat = format != nullptr ? std::optional<ibFormatString>(*format) : std::nullopt;
		m_inColumn = true;
	}
	virtual void FinishColumn() override {
		m_inColumn = false;
		m_columnFormat.reset();
	}

#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const override { return m_valueText; }
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
	// The same value as text, built once in SetValue - see Render and GetSize.
	wxString  m_valueText;
	// A boolean cell's mark, set in SetValue; empty for any other value (drawn as text).
	std::optional<bool> m_valueFlag;
	// Inside a paint pass over this column, and the format its cells are written through there — see
	// StartColumn.
	bool m_inColumn = false;
	std::optional<ibFormatString> m_columnFormat;
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
