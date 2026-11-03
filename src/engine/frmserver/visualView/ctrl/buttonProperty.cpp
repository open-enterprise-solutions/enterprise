#include "widgets.h"

void ibValueButton::OnPropertyRefresh()
{
	ibValueWindow::OnPropertyRefresh();

	// The note is drawn by a tile alone.
	HideProperty(m_propertyNote,
		m_propertyPictureLocation->GetValueAsEnum() != ibPictureLocation::ibPictureLocation_Tile);
}
