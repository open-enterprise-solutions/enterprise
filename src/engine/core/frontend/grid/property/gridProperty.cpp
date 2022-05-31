////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : grid property
////////////////////////////////////////////////////////////////////////////

#include "gridProperty.h"

CPropertyCell::CPropertyCell(wxGrid *grid) : IObjectBase(), m_ownerGrid(grid)
{
	PropertyContainer *commonGrid = IObjectBase::CreatePropertyContainer("Common");
	commonGrid->AddProperty("name", PropertyType::PT_WXNAME);
	commonGrid->AddProperty("font", PropertyType::PT_WXFONT);
	commonGrid->AddProperty("background_colour", PropertyType::PT_WXCOLOUR);

	commonGrid->AddProperty("align_horz", PropertyType::PT_OPTION, &CPropertyCell::GetOptionsAlignment);
	commonGrid->AddProperty("align_vert", PropertyType::PT_OPTION, &CPropertyCell::GetOptionsAlignment);

	commonGrid->AddProperty("text_colour", PropertyType::PT_WXCOLOUR);
	commonGrid->AddProperty("value", PropertyType::PT_WXSTRING);

	m_category->AddCategory(commonGrid);
}

void CPropertyCell::SetCellCoords(const wxGridCellCoords &coords)
{
	m_topLeftCoords = coords;
	m_bottomRightCoords = coords;

	wxString sName = wxString::Format("R%iC%i", coords.GetRow() + 1, coords.GetCol() + 1);
	IObjectBase::SetPropertyValue("name", sName);

}

void CPropertyCell::SetCellCoords(const wxGridCellCoords &topLeftCoords, const wxGridCellCoords &bottomRightCoords)
{
	m_topLeftCoords = topLeftCoords;
	m_bottomRightCoords = bottomRightCoords;

	wxString sName = wxString::Format("R%iC%i:R%iC%i", m_topLeftCoords.GetRow() + 1, m_topLeftCoords.GetCol() + 1, bottomRightCoords.GetRow() + 1, bottomRightCoords.GetCol() + 1);
	IObjectBase::SetPropertyValue("name", sName);
}

void CPropertyCell::ReadProperty() 
{
	IObjectBase::SetPropertyValue("value", m_ownerGrid->GetCellValue(m_topLeftCoords));
	IObjectBase::SetPropertyValue("font", m_ownerGrid->GetCellFont(m_topLeftCoords.GetRow(), m_topLeftCoords.GetCol()));
	IObjectBase::SetPropertyValue("background_colour", m_ownerGrid->GetCellBackgroundColour(m_topLeftCoords.GetRow(), m_topLeftCoords.GetCol()));
	IObjectBase::SetPropertyValue("text_colour", m_ownerGrid->GetCellTextColour(m_topLeftCoords.GetRow(), m_topLeftCoords.GetCol()));

	int horz, vert; 
	m_ownerGrid->GetCellAlignment(m_topLeftCoords.GetRow(), m_topLeftCoords.GetCol(), &horz, &vert);

	IObjectBase::SetPropertyValue("align_horz", horz);
	IObjectBase::SetPropertyValue("align_vert", vert);
}

void CPropertyCell::SaveProperty()
{
	if (m_topLeftCoords == m_bottomRightCoords)
	{
		m_ownerGrid->SetCellValue(m_topLeftCoords, IObjectBase::GetPropertyAsString("value"));
		m_ownerGrid->SetCellFont(m_topLeftCoords.GetRow(), m_topLeftCoords.GetCol(), IObjectBase::GetPropertyAsFont("font"));
		m_ownerGrid->SetCellBackgroundColour(m_topLeftCoords.GetRow(), m_topLeftCoords.GetCol(), IObjectBase::GetPropertyAsColour("background_colour"));
		m_ownerGrid->SetCellTextColour(m_topLeftCoords.GetRow(), m_topLeftCoords.GetCol(), IObjectBase::GetPropertyAsColour("text_colour"));
		m_ownerGrid->SetCellAlignment(m_topLeftCoords.GetRow(), m_topLeftCoords.GetCol(), IObjectBase::GetPropertyAsInteger("align_horz"), IObjectBase::GetPropertyAsInteger("align_vert"));
	}
	else
	{
		for (int row = m_topLeftCoords.GetRow(); row <= m_bottomRightCoords.GetRow(); row++)
		{
			for (int col = m_topLeftCoords.GetCol(); col <= m_bottomRightCoords.GetCol(); col++)
			{
				m_ownerGrid->SetCellValue(row, col, IObjectBase::GetPropertyAsString("value"));
				m_ownerGrid->SetCellFont(row, col, IObjectBase::GetPropertyAsFont("font"));
				m_ownerGrid->SetCellBackgroundColour(row, col, IObjectBase::GetPropertyAsColour("background_colour"));
				m_ownerGrid->SetCellTextColour(row, col, IObjectBase::GetPropertyAsColour("text_colour"));
				m_ownerGrid->SetCellAlignment(row, col, IObjectBase::GetPropertyAsInteger("align_horz"), IObjectBase::GetPropertyAsInteger("align_vert"));
			}
		}
	}
}