#include "widgets.h"

#include "backend/backend_picture.h"

static const ibString s_gauge_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAwklEQVR4nOzYMW7CQBQA0QHlQCmSJmmg4CCcjINQQAMNFNwIWnCBhYSQYOZ19roa6a+8O0VuilwBkCsAcgVArgDIFQC5AiBXAOQKgFwBkPsavvj5m535YMf9dnL93Agg1x4w9sFht+Gd/f7P7643Asi1B/Cg79Xi5vm0XL90/dkaAeQKgFwBkNMHmAxfDO8DPu0s0H3AQAGQGz0LjJ2n310jgFz/Acg1AsgVALkCIFcA5AqAXAGQKwByBUCuAMjpA1wAAAD//3Vk2zQAAAAGSURBVAMAWGQbOHYxXNsAAAAASUVORK5CYII=";

ibPicture ibValueGauge::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueGauge::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_gauge_png, ibSize(16, 16));
	return picture;
}