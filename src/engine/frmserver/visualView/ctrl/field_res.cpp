#include "field.h"

#include "backend/backend_picture.h"

// A field: its title's lines on the left, the slot its view stands in on the right — one picture whatever the view.
static const ibString s_field_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAABFElEQVR42u2auw3CMBBA2SALMAYVVSoahvEiSEaiZBk2QKxBYyoqJOOTEJUR+fqTe0+6zol1z9/EXq0AAAAAAAASsDY3E+IawicKqcuUkHiTOPGYiCangJzJfyXk7Pa+kDBFtP75cveP58vPhbxb6iiiF8SST0VMQnYBc7Z8rCcUJyA1CEAAAroL2GxbG8KF8D1DnrFVC/gk70eGrVmAm0CAQwBDgElwkIDD8eTb3f5vXVJGyi5qGZSE+krvKqEKAV1aPtYTFiNg6JyDAIYAk2DyZfDncjf2Yyj7MthzI2SnFlDbVtghYIECdA+B3JMgv8QQgAAEqBCg/mhM4+Go+uNx3Rck1F+R4ZIU1+QAAAAAAKAw3hy81P+ALI/ZAAAAAElFTkSuQmCC";

ibPicture ibValueField::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueField::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_field_png, ibSize(16, 16));
	return picture;
}
