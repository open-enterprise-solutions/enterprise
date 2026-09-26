#include "tableBox.h"

#include "backend/backend_picture.h"

static const wxString s_tableBox_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAA90lEQVR4nOzavQ7BUByG8aeNW8AVmEwSFhZuphfVK7DZrCwsJCaTK8BFkEiacBI9ac5C3/e3SL8MT3LUv2mOuBxxDoA4B0CcAyCuE+4YT+cPWuy432bv214CiPNvQOyEbq/PP7vfrrXHvQQQ5wCIcwDE+X9A7IT1akmK0/nysT0aDmgi9frJbFF73EsAcQ6AOAdAnHyALNxRPRMsy5I2KYri9elnggEHQNzXWaBaM4fdhhSeBX6cAyDOARDnAIjzLIA4B0CcZwHEOQDiHABxDoC46PsBsftoTDhTNP2+1OtjPA2GO/y2uJgMcb4NIs4BEOcAiJMP8AQAAP//KydwywAAAAZJREFUAwA0dzxaYNi7WAAAAABJRU5ErkJggg==";

wxIcon ibValueModelTableBox::GetIcon() const
{
	return ibBackendPicture::GetIconFromBase64(s_tableBox_png, wxSize(16, 16));
}

wxIcon ibValueModelTableBox::GetIconGroup()
{
	return ibBackendPicture::GetIconFromBase64(s_tableBox_png, wxSize(16, 16));
}