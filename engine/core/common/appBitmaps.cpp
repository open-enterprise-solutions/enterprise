#include "appBitmaps.h"

CAppBitmaps *CAppBitmaps::s_instance = NULL;

CAppBitmaps *CAppBitmaps::Get()
{
	return s_instance;
}

void CAppBitmaps::Destroy()
{
	wxDELETE(s_instance);
}

void CAppBitmaps::AppendImage(image_identifier_t id, const wxIcon &img)
{
	auto itFounded = m_listBMP.find(id);
	if (itFounded != m_listBMP.end()) {
		wxASSERT(itFounded != m_listBMP.end());
	}

	m_listBMP.insert_or_assign(id, img);
}

void CAppBitmaps::AppendImage(image_identifier_t id, const wxBitmap &img)
{
	auto itFounded = m_listBMP.find(id);
	if (itFounded != m_listBMP.end()) {
		wxASSERT(itFounded != m_listBMP.end());
	}

	m_listBMP.insert_or_assign(id, img);
}

wxBitmap CAppBitmaps::GetData(image_identifier_t id)
{
	auto itFounded = m_listBMP.find(id);
	if (itFounded == m_listBMP.end()) {
		wxASSERT(itFounded == m_listBMP.end());
	}

	return m_listBMP.at(id);
}

wxBitmap CAppBitmaps::GetAsBitmap(image_identifier_t id)
{
	return GetData(id);
}

wxIcon CAppBitmaps::GetAsIcon(image_identifier_t id)
{
	wxIcon convertIcon;
	convertIcon.CopyFromBitmap(GetData(id));
	return convertIcon;
}
