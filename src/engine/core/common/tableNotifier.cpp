#include "tableNotifier.h"

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