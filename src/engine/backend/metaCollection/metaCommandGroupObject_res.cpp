#include "metaCommandGroupObject.h"
#include "backend/backend_picture.h"   // ibBackendPicture::GetIconFromBase64
#include "core/picturePredefined.h"    // g_picCommandGroup*CLSID — the platform groups' pictures

/* PNG */
static const wxString s_commandGroup_64_png = wxT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAC6klEQVR4nOybTWgTQRTH/5OoLdWKtdRWGrQ91UpN/cCCh6roqZ68eS0iQg9GQUREKBEK1uKhDYIHIT17K4J6Ug8KoWBrG6TYi1FMtKloBDEfJcn4tjSwu/loyy7phnm/S3g7D+bt23mzs+9PXFAcFxSHEwDF4QRAcTgBUJxt5gsDFy9JbIKXU08FahguASgO7wHrOWy2xttvhE8hnx8UEN20mRwWAs2oIlLiFwW8IIVcoBgmoxO905X8103AhvF/3OFJ5EaklDeFEKsrayt2x7WE99PN91MsV9p982OxvW4//D0rpfxtKwFPIvuOfm4JB5UVPQg3JeSO53f2bTkfW4KlLI/QdCfhVITo81wP3ys1ZLkEPNfm+mit39Vf6+7YjTPHWtDaVI+GOjeqSTKTQzyRxpvZZSx+/asbkcP7feFnPwLeGb2/5RUgXRjS24cONmJwoAOdbTurfvMa2pza3JcvdKLrQKNhzIX8VbO/DSUguvTWueOtcArnTxhjEaZYNawnQOKI3mxrroNTaNljjEUKdJt9LO8BtMvu0tt12ysv+9uPw7CTB0PesmMN9cZY6A21z+zDR2EoDn8LwCoSS1RcbQUzmc4V1Z6eSjVrN1osBrRYTVhfAQIf9ObPPxk4BXMsEnLG7GM5AXkp5vX269k4nMKr96ZYTA9Lw3ICsi48odSmC/YnOn4GX0TwZSmJVCaPavMvlVudO/g8gsVvhqNwKpsTQbO/5T1gedz72eObow8Ncb9wTTuDG8/hWw+t1OH4o96I+botb4Fo4OgoNT+m4VAottD3QO/DUmO2vQZjTe7T1I4Zo8mqv+7LQA2RHD35UYrtbDkfGztCPStROulSS2xKOqUlJsRkbMJbpZbYGrFxb4h+QqgR+CgMxeFvAdgM6wKsC7Au4DxYF9BgXYB1AdYFWBdgXaAITgAUh3UBWIV1AdYFWBdgXYB1AdYFalYXKOpa8f8FFIMTAMWp6fq1Ay4BKI7yCfgPAAD///pTAasAAAAGSURBVAMARzmIdtEnE78AAAAASUVORK5CYII=");

// The platform's own groups — the engine's pictures (core/picturePredefined.h), each by its id.
ibPictureID ibCommandGroupPicture(ibInterfaceCommandSection area)
{
	switch (area) {
		case ibInterfaceCommandSection_Important: return g_picCommandGroupImportantCLSID;
		case ibInterfaceCommandSection_Default:   return g_picCommandGroupNormalCLSID;
		case ibInterfaceCommandSection_Create:    return g_picCommandGroupCreateCLSID;
		case ibInterfaceCommandSection_Report:    return g_picCommandGroupReportsCLSID;
		case ibInterfaceCommandSection_Service:   return g_picCommandGroupServiceCLSID;
		default:                                  return 0;
	}
}

wxIcon ibValueMetaObjectCommandGroup::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectCommandGroup::GetIconGroup()
{
	static const ibPicture picture = ibBackendPicture::FromImage(ibBackendPicture::GetImageFromBase64(s_commandGroup_64_png, wxSize(16, 16)));

	return ibBackendPicture::ToIcon(picture);
}
