#include "tableBox.h"

#include "backend/backend_picture.h"

static const ibString s_tableBoxColumn_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAABJklEQVR4nOzavWrCUBjG8X+kS+lQWmg7durUqdIu7dLeQ68heEmSK+hQ6NZVF10UnJycMqqDOIijgiBo/AiHk+15fks4eRM4eeA9nAOpIa6GOAeAOAeAOAeAuIvijdf3zxUV+l3OqdL35TUx+t12sjt2CyDOa0DZA3f3D0TJq10DQucznYzP1t0CiHMAiHMAiPM+oOyB/78fYuT1+t74sdkkRN5oEDOft4+vs3W3AOIcAOIcAOK8DzhVyLJscx0MR8S4LYwHVzeEOHg/cD7b70jT9GjdLYA4rwGnCtue6XVaxMgL45fFjBAH7z8/EcJngRIOAHEOAHEOAHE+CyDOASDOZwHEOQDEOQDEOQDEJcUb/ldYjANAXII4twDiHADiHADi5ANYAwAA//8HFQOKAAAABklEQVQDAL6nQ3uLe61QAAAAAElFTkSuQmCC";

ibPicture ibValueModelTableBoxColumn::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueModelTableBoxColumn::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_tableBoxColumn_png, ibSize(16, 16));
	return picture;
}