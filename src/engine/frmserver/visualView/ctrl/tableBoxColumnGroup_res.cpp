#include "tableBox.h"

#include "backend/backend_picture.h"

static const ibString s_tableBoxColumnGroup_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAABLUlEQVR4nOzaIWtCURjG8b+yMhbGBtvi0tKSsJWtbN9hsLR68SPJ/QQLwtqyySSYTCajGsQgRg0ieA/q5eUW5Xl+yfe89wYfeO89B60jro44B4A4B4A4B4C4i3Th5e1jtVu3l3NOydflNVX0up3abu0RQJyfAZyZu/uH0PXTyfho3yOAOAeAOAeAOO8DCHpstULX969uCnVjMSNi1GwW6v+/XyJe3z+P9j0CiHMAiHMAiPM+4FAjz/PNh5/vwnr6Xo+K3n+b3j8YErH9HlmW7e17BBDnZ8ChxnZm2sl6dC9f+SyQ1I3nJyJ8FijhABDnABDnABDnswDiHADifBZAnANAnANAnANAXPj/Aenv9WXSvfyIasre61EeAcTJB1BDnEcAcQ4AcQ4AcfIBrAEAAP//y1VA3gAAAAZJREFUAwCxOjyB0xzongAAAABJRU5ErkJggg==";

ibPicture ibValueModelTableBoxColumnGroup::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueModelTableBoxColumnGroup::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_tableBoxColumnGroup_png, ibSize(16, 16));
	return picture;
}
