#ifndef __META_PICTURE_H__
#define __META_PICTURE_H__

#include "metaObject.h"

class BACKEND_API ibValueMetaObjectPicture : public ibValueMetaObject {
	public:

	wxBitmap GetValueAsBitmap() const { return m_propertyPicture->GetValueAsBitmap(); }

	ibValueMetaObjectPicture(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:
	ibPropertyExternalPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyExternalPicture>(m_categoryContext, wxT("Picture"), _("Picture"));
};

#endif 