#ifndef __DATA_VIEW_H__
#define __DATA_VIEW_H__

#include "common/tableInfo.h"

class wxDataModelViewCtrl : public wxDataViewCtrl {
	class wxTableModelNotifier* m_genNotitfier;
protected:
	class CDataViewFreezeRowsWindow : public wxWindow
	{
		// returns the colour to be used for drawing the rules
		wxColour GetRuleColour() const {
			return wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT);
		}

		int GetLineStart(unsigned int row) const {
			return row * GetDefaultRowHeight();
		}

		int GetLineAt(unsigned int y) const {
			return y / GetDefaultRowHeight();
		}

		unsigned int GetRowCount() const {
			return 1;
		}

		int GetLineHeight(unsigned int row) const {
			return GetDefaultRowHeight();
		}

		int GetDefaultRowHeight() const;

	public:

		CDataViewFreezeRowsWindow(wxDataModelViewCtrl* parent) :
			wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 22)) {
			SetBackgroundColour(*wxWHITE);
			SetBackgroundStyle(wxBG_STYLE_PAINT);
			m_penRule = wxPen(GetRuleColour());
			wxWindow::Hide();
		}

		wxDataModelViewCtrl* GetOwner() const {
			return dynamic_cast<wxDataModelViewCtrl*>(GetParent());
		}

#if wxUSE_ACCESSIBILITY
		virtual wxAccessible* CreateAccessible() override {
			// Under MSW wxHeadrCtrl is a native control
			// so we just need to pass all requests
			// to the accessibility framework.
			return new wxAccessible(this);
		}
#endif // wxUSE_ACCESSIBILITY

	protected:
		void OnPaint(wxPaintEvent& WXUNUSED(event));
	private:

		// the pen used to draw horiz/vertical rules
		wxPen m_penRule;

		wxDECLARE_EVENT_TABLE();
		wxDECLARE_NO_COPY_CLASS(CDataViewFreezeRowsWindow);
	};
	CDataViewFreezeRowsWindow* m_freezeRows;
public:
	
	wxDataModelViewCtrl() : wxDataViewCtrl(),
		m_genNotitfier(NULL), m_freezeRows(NULL)
	{
	}

	wxDataModelViewCtrl(wxWindow* parent, wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0,
		const wxValidator& validator = wxDefaultValidator,
		const wxString& name = wxASCII_STR(wxDataViewCtrlNameStr)) : wxDataViewCtrl(parent, id, pos, size, style, validator, name),
		m_genNotitfier(NULL), m_freezeRows(NULL)
	{
		m_freezeRows = new CDataViewFreezeRowsWindow(this);
		m_windowSizer->Insert(1, m_freezeRows, 0, wxGROW);

		wxSystemThemedControl<wxControl>::DoEnableSystemTheme(true, m_freezeRows);
	}

	virtual ~wxDataModelViewCtrl() {
		wxDELETE(m_genNotitfier);
	}

	bool ShowFilter(struct filterRow_t& filter);

	virtual wxSize GetSizeAvailableForScrollTarget(const wxSize& size) override;
	virtual bool AssociateModel(wxDataViewModel* model) override;

protected:

	void OnChar(wxKeyEvent& event);
	void OnRightUp(wxMouseEvent &event); 
	void OnSize(wxSizeEvent& event);

private:

	wxDECLARE_DYNAMIC_CLASS(wxDataModelViewCtrl);
	wxDECLARE_NO_COPY_CLASS(wxDataModelViewCtrl);
	wxDECLARE_EVENT_TABLE();
};

class wxTableModelNotifier : public wxTableNotifier {
	wxDataModelViewCtrl* m_mainWindow;
public:
	wxTableModelNotifier(class wxDataModelViewCtrl* mainWindow) :
		m_mainWindow(mainWindow) {
	}

	virtual bool NotifyDelete(const wxDataViewItem& item);

	virtual wxDataViewColumn* GetCurrentColumn() const;
	virtual void StartEditing(const wxDataViewItem& item, unsigned int col) const;
	virtual bool ShowFilter(struct filterRow_t& filter);

	virtual void Select(const wxDataViewItem& item) const;
	virtual wxDataViewItem GetSelection() const;
	virtual int GetSelections(wxDataViewItemArray& sel) const;

private:

	bool SendEvent(const wxEventType& eventType, const wxDataViewItem& item);
	bool SendEvent(const wxEventType& eventType, const wxDataViewItem& item, wxDataViewColumn* column);
};

wxDECLARE_EVENT(wxEVT_DATAVIEW_ITEM_START_DELETING, wxDataViewEvent);

#endif 