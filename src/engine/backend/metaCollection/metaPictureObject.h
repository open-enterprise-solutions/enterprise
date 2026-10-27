#ifndef __META_PICTURE_H__
#define __META_PICTURE_H__

#include "metaObject.h"

class BACKEND_API ibValueMetaObjectPicture : public ibValueMetaObject {
	public:

	ibPicture GetValueAsPicture() const { return m_propertyPicture->GetValueAsPicture(); }
	// Which picture it holds — new with each its Picture is given: what its id carries (ibBackendPicture::SendPicture).
	ibGuid GetValueAsVersion() const { return m_propertyPicture->GetValueAsVersion(); }

	ibValueMetaObjectPicture(const ibString& name = ibEmptyString, const ibString& synonym = ibEmptyString, const ibString& comment = ibEmptyString);

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:
	ibPropertyExternalPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyExternalPicture>(m_categoryContext, ibT("Picture"), _("Picture"),
		_("The image this common picture holds, stored in the configuration. Commands, sections and form controls can take it as their picture by reference, so one image is kept in one place."));
};

#endif 