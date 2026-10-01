#ifndef __VALUE_PICTURE_H__
#define __VALUE_PICTURE_H__

#include "backend/compiler/value.h"
#include "backend/backend_picture.h"

//picture support
class BACKEND_API ibValuePicture : public ibValue {
	public:

	ibValuePicture(const ibPictureDescription& pictureDesc = ibPictureDescription(0));
	virtual bool Init(ibValue** paParams, const long lSizeArray);

	virtual bool IsEmpty() const {
		return m_pictureDesc.IsEmptyPicture();
	}

	const ibPictureDescription& GetPictureDesc() const { return m_pictureDesc; }

protected:

	ibPictureDescription m_pictureDesc;
};

#endif