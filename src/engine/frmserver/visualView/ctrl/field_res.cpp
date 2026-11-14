#include "widgets.h"

// A field is drawn in the designer's palette as a text box is: it is one, until its kind says otherwise.
ibPicture ibValueField::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueField::GetIconGroup()
{
	return ibValueTextCtrl::GetIconGroup();
}
