#ifndef __DVC_H__
#define __DVC_H__

#include "frmclient/win/ctrls/dataview/dataview.h"
#include "frmclient/backend/compiler/value.h"
#include "frmclient/backend/formatString.h"   // ibFormatString — what a cell is shown through (GetCellFormat)

#include <wx/renderer.h>   // wxRendererNative::DrawCheckMark — a boolean cell
#include <optional>

class ibValueModelTableBoxColumn;

// THE VALUE A CELL HOLDS — the engine's ibVariantDataValue (backend/tabularModel.h): for the reader that shows it.
class ibVariantDataValue :
	public wxVariantData {
public:
	virtual const ibValue& GetValue() const = 0;
protected:
	ibVariantDataValue() : wxVariantData() {}
};

// ----------------------------------------------------------------------------
// ibDataViewValueRenderer
// ----------------------------------------------------------------------------

// THE DESKTOP'S VALUE CELL (frontend/visualView/ctrl/tableBoxColumnRenderer.h) as the client's settings windows draw
// theirs: a value's text, a flag as a tick, edited through the text editor. The desktop's also served a table's column,
// asking it for its format and its editor's buttons and refreshing its form; a settings window hands it no column (the
// client's table draws its own cells, tableBox.cpp), so what was asked of the column is not here.
class ibDataViewValueRenderer :
	public ibDataViewCustomRenderer {
public:

	virtual void FinishSelecting() {

		// (The desktop's also refreshed the column's form here — a column's.)

		// 🛑 DELETED THROUGH THE DEFERRED QUEUE, the editor and its handler both — the desktop's, verbatim.
		if (m_editorCtrl != nullptr) {
			wxEvtHandler* const handler = m_editorCtrl->PopEventHandler();
			m_editorCtrl->Hide();
			wxPendingDelete.Append(handler);
			wxPendingDelete.Append(m_editorCtrl);
			m_editorCtrl.Release();
		}

		DoHandleEditingDone(nullptr);
	}

	explicit ibDataViewValueRenderer(ibValueModelTableBoxColumn* WXUNUSED(tableBoxColumn))
		: ibDataViewCustomRenderer(wxT("string"), wxDATAVIEW_CELL_EDITABLE, wxALIGN_LEFT)
	{
	}

	virtual bool IsCompatibleVariantType(const wxString& variantType) const { return true; }

	virtual bool Render(wxRect rect, wxDC* dc, int state) override
	{
		// A FLAG IS A TICK, as a check box shows it — the desktop's.
		if (m_valueFlag.has_value()) {
			if (*m_valueFlag) {
				wxWindow* const view = GetView();
				const wxSize mark = wxRendererNative::Get().GetCheckMarkSize(view);
				const wxRect at(rect.x, rect.y + (rect.height - mark.y) / 2, mark.x, mark.y);
				wxRendererNative::Get().DrawCheckMark(view, *dc, at);
			}
			return true;
		}

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

		if (value.IsNull()) {
			SetAlignment(wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL);
			return true;
		}

		// A CELL'S OWN VALUE or a text — the desktop's: a number to the right, a flag as a tick, the rest as it reads.
		const ibValue* cell = GetCellValue(value);
		if (cell == nullptr) {
			SetAlignment(wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL);
			m_valueText = value.MakeString();
			return true;
		}

		SetAlignment((cell->GetType() == ibValueTypes::TYPE_NUMBER ? wxALIGN_RIGHT : wxALIGN_LEFT) | wxALIGN_CENTRE_VERTICAL);
		m_valueText = cell->GetString();
		if (cell->GetType() == ibValueTypes::TYPE_BOOLEAN)
			m_valueFlag = cell->GetBoolean();
		return true;
	}

	virtual bool GetValue(wxVariant& WXUNUSED(value)) const override
	{
		return true;
	}

	static const ibValue* GetCellValue(const wxVariant& value) {
		if (value.GetType() != wxT("value"))
			return nullptr;
		return &static_cast<const ibVariantDataValue*>(value.GetData())->GetValue();
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

	wxVariant m_valueVariant;
	wxString  m_valueText;
	std::optional<bool> m_valueFlag;
};

#endif
