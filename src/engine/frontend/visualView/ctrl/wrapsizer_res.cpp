#include "sizer.h"

#include "core/graphics/picture.h"   // ibPicture::FromBase64 — the master read, scaled to 16 once

static const wxString s_wrapSizer_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAzklEQVR4nOzbsQ3CMBRF0ZuIMRiAjoEYglkYgoHoGIA9QovcWJ8icvzu6RI5zZPej2MpK+FWwhkA4U7tjfP9tTGxz+O6/F5bAcI5A3oL2s4cTW+mWQHCOQMoer630j7hdlmWPZ9v1/dYAcIZAOEMgHAGQDgDIJzfAhRV99qjPd+yAoRzBlDkecBkDIBwBkA4AyCcARDOAAjneQDhDIBwu58HjMYKEM4ACGcAhHMfQNHo7/UqK0A4Z0Bvwez/EFkBwsUHMNU7/R9WgHDxAXwBAAD//9oVq/oAAAAGSURBVAMA8u0qkKRTqd0AAAAASUVORK5CYII=";

ibPicture ibValueWrapSizer::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueWrapSizer::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_wrapSizer_png, ibSize(16, 16));
	return picture;
}
