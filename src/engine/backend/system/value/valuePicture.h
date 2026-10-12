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

// Named global PictureLib. Properties are the pictures registered with the process.
// Not a context type: it is bound by name, and a script sees the name.
class BACKEND_API ibValuePictureLib : public ibValueDynamicMembers {
public:

	ibValuePictureLib();

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	void FillMembers(ibMemberTable& helper) const;

};

// One library for the process. CreateMainModule binds it; DestroyMainModule unbinds the name.
BACKEND_API ibValue* ibPictureLibrary();

#endif