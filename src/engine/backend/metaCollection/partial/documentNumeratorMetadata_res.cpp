#include "documentNumerator.h"

/* PNG */
static const ibString s_documentNumerator_16_png = ibT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAABSElEQVR42u3bsWrCUBTGcR+koM/QOBiX3rWbg+Dg4OLQTTp27RP0ORzE4lu4OaiDIA4+gRYUJNwaKIUoFiXm3HN7/x98BHRI7o/LCQmkVCKEkD9SrZtmFD/Njkfrsuk1PNZMWxzgeOKp68VnIGqmK70DrLaKImgE+EF4cQIgHec7QTOACIJ2gMIRtAEMPkeyCNoA0ogiaAQQRdAKIIagGUAEwYe7wKXe5dnBZ4AoNqugAdIC4DtAXjAAAABABuC50bprAQAAAIYgAAAAEO4QzDsMLwUAAADwZwgekvPfyp1hprf+7wXAePllGx8LW3mdhAWw3Sf2rb+2D73Jb4MB2OwSW3+fZxYfFMC1yQtwMwgAAAAgCnC6gKILAAAAMAQBAAAAhiAAAADg5oMJ74ZgcABFfzTlww5oRrFZBAvgOs5figIAwP8G+AYcTgZO3UmQdAAAAABJRU5ErkJggg==");

ibPicture ibValueMetaObjectDocumentNumerator::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectDocumentNumerator::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_documentNumerator_16_png, ibSize(16, 16));

	return picture;
}
