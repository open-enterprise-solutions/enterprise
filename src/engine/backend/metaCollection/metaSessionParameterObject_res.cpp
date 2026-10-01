#include "metaSessionParameterObject.h"

// THE ATTRIBUTE'S SHAPE, IN ANOTHER COLOUR. A session parameter IS an attribute —
// same editor, same type page, same everything a declaration needs — so the icon
// keeps the attribute's outline and swaps its blue for amber. A different picture
// would claim a different kind of thing; the same picture would hide that this one
// belongs to the session rather than to a table.
//
// Placeholder in the honest sense: the attribute icon with R and B swapped, good
// enough to tell the branches apart until somebody draws one on purpose.

/* PNG */
static const wxString s_sessionParameter_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAADuElEQVR4nOyZSUzUUBjH/28GKDOCLCqrSwANIrIcBIREJBE1hBtc9KSJicblpizeMCEBkbsxHkw4eDTGREU9kRgBRZEIuERFNGyyCAFmGJhOfS0Jtp0WMgxhOr7+km7f97V579/3+l6/ZwHjWMA4pgBgHFMAMI4pABjHFACMYwoAxjEFAOOYAoBxQtYKGLuGQt6Cs/Q0QyA4QIBtMDACMEkE9IOg32LBvYQGdK4WT3QfVIewYQfqCcFVBGtLEcDTfVOiHXWkDotaIbotYMSJl7TyeQhmCKx0f33EgWP0WKAVovlmh6tRTw/BXXk5BPlDNbih7VIxUo182tcV/YZLy0FE/gmEbk8CCbfDyAgLDixNDGPudStc3z4onQSHkhrxVm7yagEegovyay41G7EVlxC2c6/hKy8illEsa2zFFXApB5VOD86r472/AQLS5e0isrAMa8LzcA1+hHtqbPmhsfHg9mQAVisCSWRROVwDvXJTujrGSwD64cuSX1tps1+NpdEfmG5tgXt8SPngHcmIKT8nHQNFSEy82pShNmh9BCMUAWHh0ENYdOHPo7telRcRbVMPb0sxgYLYtqgMiFPH+DW+O963gZ+Z0PXz0+NSjJHxS4DFscENiQkka06FV2Np7NeGxAQSv1qAbV/uhsQEEr8EsGcfAQnldP2iz55TDCPjVxewxuzA1pJKzLy4r+mPKj0Fa/R2rIeRWxd8ik+suoP14JcAIvbco9KMa+7N85WpJ5eWjYi847BGGfrPWcJvAUTEikaVngbELcgwM0IwKOvt075iJkXBOOvvArwbC1974PzUhaXfP+m8fxJSSlKC5qKityE0bjdsmYcRnpJJv5TG7G2+l0rwYK7zmTTsidkXnSAqyIS0LXx5R5MUWxBRcFIaGkGM1eh8FmD6aQucfe0+3SMszGO27QHck6OILjsDI+H9OgSMKi6d8yvnru+9PldejrP3lfSMzUJe9mWDsm4i3gIQdMsv3VP/7nH0d8BfnJ+7sFnIyy5BlAlREa0u0EO3lUTgbEcrYisvS+fcrv1aaSafCNnE6fFs+2O1qVtt8EqLj9YilRfQRx0ruTAuLQuRBWUIEdPinA1GxuOcA0+Ts7MdTxTdTRDgtHqQmdCMAXm85tLYcA1q6aEB/xG0olWJN9GsYdeGrqR0EJ3lpGCDvv325CYUafl0B+UkG4rptKYJ4nJCsLK8ONqYZEeJXgjBGjC7PM4K5t8gGMcUAIxjCgDGMQUA45gCgHFMAcA4pgBgHOYF+AsAAP//s5UEJAAAAAZJREFUAwCTtw7vd7Af1gAAAABJRU5ErkJggg==");

wxIcon ibValueMetaObjectSessionParameter::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectSessionParameter::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_sessionParameter_16_png, wxSize(16, 16));

	return icon;
}