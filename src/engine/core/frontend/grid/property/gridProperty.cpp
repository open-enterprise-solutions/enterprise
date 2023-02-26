////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : grid property
////////////////////////////////////////////////////////////////////////////

#include "gridProperty.h"
#include "frontend/grid/gridCommon.h"
#include "frontend/mainFrame.h"

void CGridPropertyObject::AddSelectedCell(const wxGridBlockCoords& coords, bool afterErase)
{
	if (this != objectInspector->GetSelectedObject())
		return;

	if (afterErase)
		ClearSelectedCell();

	m_currentBlocks.push_back(coords);

	objectInspector->SelectObject(this);
	m_ownerGrid->ForceRefresh();
}

void CGridPropertyObject::ShowProperty()
{
	CGridPropertyObject::ClearSelectedCell(); bool hasBlocks = false;

	for (auto& coords : m_ownerGrid->GetSelectedBlocks()) {
		m_currentBlocks.push_back(coords); hasBlocks = true;
	}
	if (!hasBlocks) {
		m_currentBlocks.push_back({
			m_ownerGrid->GetGridCursorRow(), m_ownerGrid->GetGridCursorCol(),
			m_ownerGrid->GetGridCursorRow(), m_ownerGrid->GetGridCursorCol()
			}
		);
	}

	objectInspector->SelectObject(this);
}

void CGridPropertyObject::OnPropertyCreated(Property* property, const wxGridBlockCoords& coords)
{
	if (m_propertyName == property) {
		if (coords.GetTopLeft() != coords.GetBottomRight()) {
			wxString nameField = wxString::Format("R%iC%i:R%iC%i",
				coords.GetTopRow() + 1,
				coords.GetLeftCol() + 1,
				coords.GetBottomRow() + 1,
				coords.GetRightCol() + 1
			);
			m_propertyName->SetValue(nameField);
		}
		else {
			wxString nameField = wxString::Format("R%iC%i",
				coords.GetTopRow() + 1,
				coords.GetLeftCol() + 1
			);
			m_propertyName->SetValue(nameField);
		}
	}
	else if (m_propertyText == property) {
		m_propertyText->SetValue(
			m_ownerGrid->GetCellValue(coords.GetTopRow(), coords.GetLeftCol())
		);
	}
	else if (m_propertyAlignHorz == property) {
		int horz, vert;
		m_ownerGrid->GetCellAlignment(coords.GetTopRow(), coords.GetLeftCol(), &horz, &vert);
		m_propertyAlignHorz->SetValue(horz);
		m_propertyAlignVert->SetValue(vert);
	}
	else if (m_propertyOrient == property) {
		m_propertyOrient->SetValue(
			m_ownerGrid->GetCellTextOrientation(coords.GetTopRow(), coords.GetLeftCol())
		);
	}
	else if (m_propertyFont == property) {
		m_propertyFont->SetValue(m_ownerGrid->GetCellFont(coords.GetTopRow(), coords.GetLeftCol()));
	}
	else if (m_propertyBackgroundColour == property) {
		m_propertyBackgroundColour->SetValue(m_ownerGrid->GetCellBackgroundColour(coords.GetTopRow(), coords.GetLeftCol()));
	}
	else if (m_propertyTextColour == property) {
		m_propertyTextColour->SetValue(m_ownerGrid->GetCellTextColour(coords.GetTopRow(), coords.GetLeftCol()));
	}
	else if (m_propertyLeftBorder == property) {
		wxColour borderColour;
		wxPenStyle leftBorder, rightBorder,
			topBorder, bottomBorder;
		m_ownerGrid->GetCellBorder(coords.GetTopRow(), coords.GetLeftCol(),
			&leftBorder, &rightBorder, &topBorder, &bottomBorder,
			&borderColour
		);
		m_propertyLeftBorder->SetValue(leftBorder);
		m_propertyRightBorder->SetValue(rightBorder);
		m_propertyTopBorder->SetValue(topBorder);
		m_propertyBottomBorder->SetValue(bottomBorder);
		m_propertyColourBorder->SetValue(borderColour);
	}
}

void CGridPropertyObject::OnPropertyChanged(Property* property, const wxGridBlockCoords& coords)
{
	for (int col = coords.GetLeftCol(); col <= coords.GetRightCol(); col++) {
		for (int row = coords.GetTopRow(); row <= coords.GetBottomRow(); row++) {
			if (m_propertyText == property) {
				m_ownerGrid->SetCellValue(row, col, m_propertyText->GetValueAsString());
			}
			else if (m_propertyFont == property) {
				m_ownerGrid->SetCellFont(row, col, m_propertyFont->GetValueAsFont());
			}
			else if (m_propertyBackgroundColour == property) {
				m_ownerGrid->SetCellBackgroundColour(row, col, m_propertyBackgroundColour->GetValueAsColour());
			}
			else if (m_propertyTextColour == property) {
				m_ownerGrid->SetCellTextColour(row, col, m_propertyTextColour->GetValueAsColour());
			}
			else if (m_propertyAlignHorz == property || m_propertyAlignVert == property) {
				m_ownerGrid->SetCellAlignment(row, col, m_propertyAlignHorz->GetValueAsInteger(), m_propertyAlignVert->GetValueAsInteger());
			}
			else if (m_propertyOrient == property) {
				m_ownerGrid->SetCellTextOrientation(row, col, (wxOrientation)m_propertyOrient->GetValueAsInteger());
			}
			else if (m_propertyLeftBorder == property || m_propertyRightBorder == property ||
				m_propertyTopBorder == property || m_propertyBottomBorder == property ||
				m_propertyColourBorder == property) {
				m_ownerGrid->SetCellBorder(row, col,
					(wxPenStyle)m_propertyLeftBorder->GetValueAsInteger(), (wxPenStyle)m_propertyRightBorder->GetValueAsInteger(),
					(wxPenStyle)m_propertyTopBorder->GetValueAsInteger(), (wxPenStyle)m_propertyBottomBorder->GetValueAsInteger(),
					m_propertyColourBorder->GetValueAsColour()
				);
			}
		}
	}
}

CGridPropertyObject::CGridPropertyObject(CGrid* ownerGrid) : IPropertyObject(),
m_ownerGrid(ownerGrid)
{
}

CGridPropertyObject::~CGridPropertyObject()
{
	if (objectInspector->GetSelectedObject() == this) {
		objectInspector->ClearProperty();
	}
}

void CGridPropertyObject::OnPropertyCreated(Property* property)
{
	for (auto& coords : m_currentBlocks) {
		CGridPropertyObject::OnPropertyCreated(property, coords);
	}
}

void CGridPropertyObject::OnPropertyChanged(Property* property)
{
	int maxRow = m_ownerGrid->GetGridCursorRow(), maxCol = m_ownerGrid->GetGridCursorCol();
	for (auto& coords : m_currentBlocks) {
		if (maxRow < coords.GetBottomRow())
			maxRow = coords.GetBottomRow();
		if (maxCol < coords.GetRightCol())
			maxCol = coords.GetRightCol();
		CGridPropertyObject::OnPropertyChanged(property, coords);
	}

	m_ownerGrid->SendPropertyModify({ maxRow, maxCol });
	m_ownerGrid->ForceRefresh();
}