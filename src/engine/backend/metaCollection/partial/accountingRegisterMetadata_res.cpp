#include "accountingRegister.h"

/* PNG - accounting register icon 16x16 */
static const wxString s_accountingRegister_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAMAAAAoLQ9TAAAAA3NCSVQICAjb4U/gAAAAMFBMVEUAAAGha02lbk2hakqkbU3ennLbmWz/1bj/xJylcFCibUyibU3x6eX///+nclahbEvzIaCgAAAAAXRSTlMAQObYZgAAAC9JREFUGJVjYGBkYkYCDAwMLKxs7BwwABJgZhsqApxcaH7h5uFFAJAAHz+aCjQAAPm1BXSM41l5AAAAAElFTkSuQmCC");

wxIcon ibValueMetaObjectAccountingRegister::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectAccountingRegister::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_accountingRegister_16_png, wxSize(16, 16));
	return icon;
}
