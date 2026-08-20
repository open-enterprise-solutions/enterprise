////////////////////////////////////////////////////////////////////////////
//	Gridbox — ITS PROPERTIES: what changing one does
////////////////////////////////////////////////////////////////////////////

#include "gridBox.h"

// A SOURCE CHANGE IS THE SAME EVENT THE TABLE ANSWERS (tableBoxProperty.cpp) — and the only one this
// box has to answer: a different source is a different MODEL, and a different model holds a different
// sheet. RefreshModel is where that is done, once, for all three moments a binding can change.
//
// (No "refill the columns?" question here: a gridbox has no columns to refill. What a sheet contains
//  is the designer's own and no source dictates it.)
void ibValueGridBox::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (property == m_propertySource)
		RefreshModel();

	ibValueWindowComposite::OnPropertyChanged(property, oldValue, newValue);
}
