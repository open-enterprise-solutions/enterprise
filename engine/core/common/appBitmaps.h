#ifndef _APPBITMAPS_H__
#define _APPBITMAPS_H__

#include <wx/bitmap.h>
#include <wx/icon.h>

#include <map>

typedef int image_identifier_t;

#define appBitmaps         	(CAppBitmaps::Get())
#define appBitmapsDestroy() (CAppBitmaps::Destroy())

class CAppBitmaps {
	static CAppBitmaps *s_instance;
private:
	CAppBitmaps() {}
	void AppendImage(image_identifier_t id, const wxIcon &img);
	void AppendImage(image_identifier_t id, const wxBitmap &img);
	wxBitmap GetData(image_identifier_t id);
public:
	static CAppBitmaps* Get();
	static void Destroy();
	wxBitmap GetAsBitmap(image_identifier_t id);
	wxIcon GetAsIcon(image_identifier_t id);
private:
	std::map<image_identifier_t, wxBitmap> m_listBMP; 
};

#endif
