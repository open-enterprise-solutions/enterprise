#ifndef _FRMCLIENT_FORMAT_CONSTRUCTOR_DLG_H__
#define _FRMCLIENT_FORMAT_CONSTRUCTOR_DLG_H__

#include "frmclient/win/dlgs/translateConstructor/translateConstructor.h"   // ibBoxEditor — a language's box, edited

// THE FORMAT STRING CONSTRUCTOR — the desktop's (frontend/win/dlgs/formatConstructor): the window a format property
// opens over each language's box. A format is written in the designer — an attribute's, a column's — and no object of
// the client's has one to show, so its boxes are plain.
inline ibDialogTranslateConstructor::ibBoxEditor ibFormatBoxEditor(bool WXUNUSED(readOnly))
{
	return ibDialogTranslateConstructor::ibBoxEditor();
}

#endif
