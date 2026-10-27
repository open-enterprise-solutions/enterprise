#include "enumeration.h"

/* PNG */
static const ibString s_enum_64_png = ibT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAABcklEQVR4nOyYvUoDQRSFzx0SfAxfQSGCGAWrFKaxs7IQFisLH0c0ndrHYgutrEQkYF7Bd7ATHUcDqbJ7ZyAks3vPVyTL7mFZzv7c4XMwjoNxWACMwwJgHBYA47AAGKeTEh71ylMIjgG/O9sjr/AYF5PhLRqKxIRG2+UmurgOm4OKyBO+cF68Dz/QMOKegA7uw2+/JjFA19+F/4OqQG/v0GMNTF6ea2+y+g242SnPwnPSh4rs/2cbhv4RFH+CWFKymRDzCmwhnpRsFnAdEJGZIp6UbBaoBQjcFSJJyeaCWkDxdjT28I9aLsy4h78sGkbUOuDn2186J1MRbCw6Hgr6hLiLunNo83hdJF2U2aVwm+EYhHHoA1LC9AH0AZXQB9AHpGYzgT4AxqEP0AL0AfQB9AFz6ANaCMcgjEMfkBKmD6APqGRlPmDZ6wn6ADVBH0Af0GroA7QAfQB9AH3AHPqAFsIxCOOwABiHBcA4LADGMV/ALwAAAP//Kr2c3AAAAAZJREFUAwCgxLFRjB2ybAAAAABJRU5ErkJggg==");

ibPicture ibValueMetaObjectEnumeration::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectEnumeration::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_enum_64_png, ibSize(16, 16));

	return picture;
}
