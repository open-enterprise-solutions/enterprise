#include "sizer.h"

#include "backend/backend_picture.h"

static const wxString s_gridSizer_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAA0ElEQVR4nOzbsQ3CMBhE4UfEGMwAc1FRMgMlFXPBDOwBLXJjGQsU+97XJbJSnHR/nChZCLcQzgAIty1P7E73FxN7Xg+bz2MrQDhnQG1B2ZnR1GaaFSCcM4BGa98ntM4sK0A4ZwCdLsd90/rz7fHX69VYAcIZAOEMgHAGQDgDIJzPAnTq3Yv/+no1VoBwBkA4AyCc+wA6+U5wcAZAOAMgnAEQzgAIZwCE850g4QyAcM0zYPRvh0tWgHDOgNqC2f8hsgKEiw9gqnv6N6wA4eIDeAMAAP//TU1rmwAAAAZJREFUAwBCUiQS6haR6AAAAABJRU5ErkJggg==";

wxIcon ibValueGridSizer::GetIcon() const
{
	return ibBackendPicture::GetIconFromBase64(s_gridSizer_png, wxSize(16, 16));
}

wxIcon ibValueGridSizer::GetIconGroup()
{
	return ibBackendPicture::GetIconFromBase64(s_gridSizer_png, wxSize(16, 16));
}