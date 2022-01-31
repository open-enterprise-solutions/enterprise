#ifndef _DVC_H__
#define _DVC_H__

#include <wx/dataview.h>
#include <wx/dvrenderers.h>

// ----------------------------------------------------------------------------
// CValueViewRenderer
// ----------------------------------------------------------------------------

class CValueTableBoxColumn;

class CValueViewRenderer : public wxDataViewCustomRenderer {
	CValueTableBoxColumn *m_colControl;
public:

	void FinishSelecting(wxVariant &valVariant) {

		if (m_editorCtrl) {
			// Remove our event handler first to prevent it from (recursively) calling
			// us again as it would do via a call to FinishEditing() when the editor
			// loses focus when we hide it below.
			wxEvtHandler *const handler = m_editorCtrl->PopEventHandler();

			// Hide the control immediately but don't delete it yet as there could be
			// some pending messages for it.
			m_editorCtrl->Hide();

			wxPendingDelete.Append(handler);
			wxPendingDelete.Append(m_editorCtrl);

			// Ensure that DestroyEditControl() is not called again for this control.
			m_editorCtrl.Release();
		}

		//DoHandleEditingDone(&valVariant);
		DoHandleEditingDone(NULL);
	}

	// This renderer can be either activatable or editable, for demonstration
	// purposes. In real programs, you should select whether the user should be
	// able to activate or edit the cell and it doesn't make sense to switch
	// between the two -- but this is just an example, so it doesn't stop us.
	explicit CValueViewRenderer(CValueTableBoxColumn *col)
		: wxDataViewCustomRenderer(wxT("string"), wxDATAVIEW_CELL_EDITABLE, wxALIGN_LEFT), m_colControl(col)
	{
	}

	virtual bool Render(wxRect rect, wxDC *dc, int state) override
	{
		RenderText(m_valueVariant,
			0, // no offset
			rect,
			dc,
			state);

		return true;
	}

	virtual bool ActivateCell(const wxRect& cell,
		wxDataViewModel *model,
		const wxDataViewItem &item,
		unsigned int col,
		const wxMouseEvent *mouseEvent) override
	{
		return false;
	}

	virtual wxSize GetSize() const override
	{
		if (!m_valueVariant.IsNull()) {
			return GetTextExtent(m_valueVariant);
		}
		else {
			return GetView()->FromDIP(wxSize(wxDVC_DEFAULT_RENDERER_SIZE,
				wxDVC_DEFAULT_RENDERER_SIZE));
		}
	}

	virtual bool SetValue(const wxVariant &value) override
	{
		m_valueVariant = value.GetString();
		return true;
	}

	virtual bool GetValue(wxVariant &WXUNUSED(value)) const override
	{
		return true;
	}

#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const override
	{
		return m_valueVariant;
	}
#endif // wxUSE_ACCESSIBILITY

	virtual bool HasEditorCtrl() const override { return true; }

	virtual wxWindow* CreateEditorCtrl(wxWindow* parent,
		wxRect labelRect,
		const wxVariant& value) override;

	virtual bool GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value) override;

private:

	wxVariant m_valueVariant;
};

// ----------------------------------------------------------------------------
// CDataViewColumnObject
// ----------------------------------------------------------------------------

class CDataViewColumnObject : public wxObject,
	public wxDataViewColumn {
	unsigned int m_controlId;
public:

	CDataViewColumnObject(CValueTableBoxColumn *col,
		const wxString& title,
		unsigned int model_column,
		int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE)
		: wxDataViewColumn(title, new CValueViewRenderer(col), model_column, width, align, flags), m_controlId(0)
	{
	}

	CDataViewColumnObject(CValueTableBoxColumn *col,
		const wxBitmap& bitmap,
		unsigned int model_column,
		int width = wxDVC_DEFAULT_WIDTH,
		wxAlignment align = wxALIGN_CENTER,
		int flags = wxDATAVIEW_COL_RESIZABLE)
		: wxDataViewColumn(bitmap, new CValueViewRenderer(col), model_column, width, align, flags), m_controlId(0)
	{
	}

	CValueViewRenderer* GetRenderer() const { 
		return dynamic_cast<CValueViewRenderer *>(m_renderer); 
	}

	void SetControlID(unsigned int obj_id) { wxASSERT(m_controlId == 0); m_controlId = obj_id; }
	unsigned int GetControlID() const { return m_controlId; }

	void SetColModel(unsigned int col_model) {
		m_model_column = col_model;
	}
};

#endif // !_DVC_H__
