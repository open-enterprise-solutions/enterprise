#ifndef __VALUE_PICTURE_H__
#define __VALUE_PICTURE_H__

#include "backend/compiler/value.h"
#include "backend/backend_picture.h"

constexpr ibClassID g_valuePictureCLSID = value_to_clsid("SY_PICTR");

#include <vector>

class ibMetaData;

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

// PictureLib — the pictures a script can name: the configuration's own (its Picture objects) and the ones the engine
// offers, the same set the picture property's picker offers. One per module manager, made with its main module and
// bound by name beside the scope containers. A property is one picture; its id is kept when the members are filled.
class BACKEND_API ibValuePictureLib : public ibValueDynamicMembers {
public:

	explicit ibValuePictureLib(const ibMetaData* metaData);

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor

private:

	const ibMetaData* m_metaData;

	// the picture of each property, by the property's data — filled with the members
	mutable std::vector<ibPictureDescription> m_pictures;
};

#endif
