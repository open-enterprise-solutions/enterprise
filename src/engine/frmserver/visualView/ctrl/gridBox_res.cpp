#include "gridBox.h"

#include "backend/backend_picture.h"

static const ibString s_gridBox_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAA+klEQVR4nOzbMQ4BQQCF4Z+4Ak6gUkloaLjMHmpPoNNpaWhIVConwCGoJHYSJpvZjY33vm6QKf7M2p1Jto24NuIcAHGd8IPxdP7gjx3329b72CuAhsnzvDDOsow6eQXEftDt9SljvVqS4nS+FMaH3YYUk9ni6/e+DSLOARDnAIhzAMQ5AOK8F6Bi4bP8r+d77S4/7Sp9CSDO/wFUbDQckCK85lPn83lAhAMgzgEQ5wCIcwDEOQDifB6AOAdAnM8DEOcAiHMAxDkA4hwAcQ6AOAdAnAMgLnoecL9dKSO2/44J3xhJnS/GK4CGqfsdoZD8CmghzrdBxMkHeAIAAP//wef5egAAAAZJREFUAwC/CS7gV6/leQAAAABJRU5ErkJggg==";

ibPicture ibValueGridBox::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueGridBox::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_gridBox_png, ibSize(16, 16));
	return picture;
}