#include "sizer.h"

#include "backend/backend_picture.h"

static const wxString s_boxSizer_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAyklEQVR4nOzbsQ3CMBhE4UfEGMwAc1FRMgMlFXPBDOwBLXJjGReJfe/rErk66f44VrIQbiGcARBuX944XJ4fJva+n3a/11aAcM6A2oKyM6OpzTQrQDhnAI22vk9onVlWgHDOADrdzkfWdH286GEFCGcAhDMAwhkA4QyAcL4L0Kl3L742K0A4AyCcARDOfQCdPBMcnAEQzgAIZwCEMwDCGQDhPBMknAEQrnkGjP7tcMkKEM4ZUFsw+z9EVoBw8QFM9Uz/hxUgXHwAXwAAAP//3/WWFQAAAAZJREFUAwCCKhpS8aAc6wAAAABJRU5ErkJggg==";

wxIcon ibValueBoxSizer::GetIcon() const
{
	return ibBackendPicture::GetIconFromBase64(s_boxSizer_png, wxSize(16, 16));
}

wxIcon ibValueBoxSizer::GetIconGroup()
{
	return ibBackendPicture::GetIconFromBase64(s_boxSizer_png, wxSize(16, 16));
}