#include "clsid.h"

void CLSID2TEXT(CLASS_ID id, const wxString &clsidText)
{
	clsidText[8] = 0;
	
	for (int i = 7; i >= 0; i--) { 
		clsidText[i] = char(id & 0xff); id >>= 8;
	}
}

CLASS_ID TEXT2CLSID(const wxString &clsidText)
{
	wxASSERT(clsidText.length() <= 8);
	char buf[9] = { 0 };
	strncpy_s(buf, sizeof(buf), clsidText.GetData(), clsidText.length());
	
	size_t need = 8 - strlen(buf);
	while (need) { 
		buf[8 - need] = ' ';
		need--;
	}
	
	return MK_CLSID(
		buf[0], 
		buf[1],
		buf[2], 
		buf[3],
		buf[4],
		buf[5], 
		buf[6],
		buf[7]
	);
}

