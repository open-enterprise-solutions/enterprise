#include "value.h"

#include "frmclient/backend/backend_picture.h"   // ibBackendPicture::GetImageFromBase64 — a master in the source

// THE ENGINE'S OWN PICTURE OF A VALUE — backend/compiler/value_res.cpp, the same PNG: a field of a settings window wears it.
/* PNG */
static const ibString s_value_16_png = ibT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAACO0lEQVR4nOyYO08UURTH/4dlMbEhmuiOQmXUSmUT46PW4IMKtYXCxFhYrIX6KYyJa2tDIZ3x0WAIfAAiIewmVIRAw2N4hEe5C+zh3oJw5wEUMztLcs6v2Oy9M3P+d36ZmcycNginDcJRARCOCoBwVACEowIgHBUA4agACEcFQDgqAMJRARBO+0kbu0qVQSLqB/NDEF1FEpiXTY0J8+f3Yrn44yzkWShu8tK7Ga8jvz9sNj5CE2Dw2C5yg2vl26utyHOJvQU68ns/m7UYC4F689wYblVecFuI7lL1jZn9jgxgotfU4PYs85a+3hly5+KeAa/cwa1rnXh638PlC+eQhLWtGkb/+5iZ3zmabPBL85t39+vr6cSnPg/XC8ny5lZr+DziY6QayRty94u7BYru4EkKJ2+xNXrveYE5At8N531M4eQttsaH57F5AaJXACFwVCGFkz/EuxiqFfOkv1FIL++md3qevghBOCoAwlEBEI4KgHBUAISjAiIzDN8d+ps1pEW0Fq+E82b99PKitUxeiKgAwrQ7HJv0sbFdR1Ls57CtFVgOMBXO+/LPx8J68jz7OWxrRfJCRBoiV0qVFzmiX8iAffAzs6rzWeatlIuj7lxsT7CrVB0nwmM0E8bfxW89/a3Ic4l9CNb3cgPMGEfT1mKalHW8bVWeC510YPf7yoA52raRHiRuU4OXGDRhujJ/jmtTZ51nIQhH3wMgHBUA4agACEcFQDgqAMJRARCOCoBwVACEowIgHPECDgAAAP//hE3VjwAAAAZJREFUAwD0h80YNiTWzgAAAABJRU5ErkJggg==");

wxIcon ibValue::GetIconGroup()
{
	// …at 16 by 16, as the engine reads it.
	static const wxIcon icon = [] {
		const wxImage image = ibBackendPicture::GetImageFromBase64(s_value_16_png, wxSize(16, 16));
		wxIcon made;
		if (image.IsOk())
			made.CopyFromBitmap(wxBitmap(image));
		return made;
	}();
	return icon;
}
