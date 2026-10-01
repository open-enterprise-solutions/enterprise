#include "enumeration.h"

/* PNG */
static const wxString s_enum_64_png = wxT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAABcklEQVR4nOyYvUoDQRSFzx0SfAxfQSGCGAWrFKaxs7IQFisLH0c0ndrHYgutrEQkYF7Bd7ATHUcDqbJ7ZyAks3vPVyTL7mFZzv7c4XMwjoNxWACMwwJgHBYA47AAGKeTEh71ylMIjgG/O9sjr/AYF5PhLRqKxIRG2+UmurgOm4OKyBO+cF68Dz/QMOKegA7uw2+/JjFA19+F/4OqQG/v0GMNTF6ea2+y+g242SnPwnPSh4rs/2cbhv4RFH+CWFKymRDzCmwhnpRsFnAdEJGZIp6UbBaoBQjcFSJJyeaCWkDxdjT28I9aLsy4h78sGkbUOuDn2186J1MRbCw6Hgr6hLiLunNo83hdJF2U2aVwm+EYhHHoA1LC9AH0AZXQB9AHpGYzgT4AxqEP0AL0AfQB9AFz6ANaCMcgjEMfkBKmD6APqGRlPmDZ6wn6ADVBH0Af0GroA7QAfQB9AH3AHPqAFsIxCOOwABiHBcA4LADGMV/ALwAAAP//Kr2c3AAAAAZJREFUAwCgxLFRjB2ybAAAAABJRU5ErkJggg==");

wxIcon ibValueMetaObjectEnumeration::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectEnumeration::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_enum_64_png, wxSize(16, 16));

	return icon;
}
