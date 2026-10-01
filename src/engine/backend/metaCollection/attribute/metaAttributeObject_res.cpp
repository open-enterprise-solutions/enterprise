#include "metaAttributeObject.h"

/* PNG */
static const wxString s_attribute_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAACgklEQVR4nOyZz2sTURDHv7NRW6oVa6ktZNH2VCPa4K+KHlT0pH9F8SD0YCtIUW8RCmrx0l48CPGv0Jt6UFDB1iZIsacqGm0VjSDmh2T3+VYo7E8DsYfdzHwuYd7MPma+ee9l88YAcwwwRwQAc0QAMEcEAHNEADBHBABzRAAwRwQAczY1C0hfLh6HbY8RKKOAfUToRYxRCt8IWFKklnTO9z/OZl/+K54iPbk3W8yyNa2LvkIJXSlKKUuXOFPamcoht/93WExkYWa58Ux/TCW1eAciSukVe9383ngaFRNaXHqiMK0fP4p2gWjUnCzeCHX5B8xLi6MwyLNvMoPbcepgH/p7OtHVkUKcqdQtrJVreLLwBcvvf3p8lqIjn+dG5t1jgRWgDIy77b17ujF2bhBDA1tjX7yDk6OT64XzQxje3e3xGbAv+uNDtgANu60zh/qRVM4e9uZOvtocggIoHHCbA70dSCp9O7y5K0LGHxN4D9Cn5ja33bE5/ss+iq5Ob+76wNvlj5FXYTBHBABz5N8gWuTq3SL+h9vjI9jI+ZvNF4VsATBHzgC0SKt7Li7zryNbAMwRAcAcESAworDqNis1C0klkLuvNoegAITXbvPrjzqSij93BTXvjwkIYCsquO3HC2tIKo9e+XL3fbkOAQEaBu5pqWrr9lt9tZx/uIJ3qxVU6zbizq+q9TfX/IMVLH9wX4urasOivD8+tDVmTixe05eDN9FG2ApTn+ayd/zjkb3B9GThhXYeQxug+5vPS7PZE2G+yJ/BUk/qpO4uzuiH47/uI3Cao/pMu6VrOR0VQ80m4dseZ4K8CYI5IgCYIwKAOSIAmCMCgDkiAJgjAoA57AX4AwAA//+aWaB0AAAABklEQVQDAF2xrq5Kcze+AAAAAElFTkSuQmCC");

// THE DEFAULT COLUMN PICTURE — declared on the column face (query/queryColumn.h) and defined here,
// beside the picture it hands out. Any column that is not a metaobject — a view's column, a temp
// table's, a synthetic projection — reads as a plain attribute, which is what it is from the
// reader's point of view. Metaobject columns override and wear their own.
wxIcon ibBackendSourceColumn::GetColumnIcon() const
{
	return ibValueMetaObjectAttribute::GetIconGroup();
}

wxIcon ibValueMetaObjectAttribute::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectAttribute::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_attribute_16_png, wxSize(16, 16));

	return icon;
}
