#include "sizer.h"

#include "backend/backend_picture.h"

static const ibString s_boxSizer_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAyklEQVR4nOzbsQ3CMBhE4UfEGMwAc1FRMgMlFXPBDOwBLXJjGReJfe/rErk66f44VrIQbiGcARBuX944XJ4fJva+n3a/11aAcM6A2oKyM6OpzTQrQDhnAI22vk9onVlWgHDOADrdzkfWdH286GEFCGcAhDMAwhkA4QyAcL4L0Kl3L742K0A4AyCcARDOfQCdPBMcnAEQzgAIZwCEMwDCGQDhPBMknAEQrnkGjP7tcMkKEM4ZUFsw+z9EVoBw8QFM9Uz/hxUgXHwAXwAAAP//3/WWFQAAAAZJREFUAwCCKhpS8aAc6wAAAABJRU5ErkJggg==";

ibPicture ibValueBoxSizer::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueBoxSizer::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_boxSizer_png, ibSize(16, 16));
	return picture;
}