#ifndef _GENERATION_DATA_WND_H__
#define _GENERATION_DATA_WND_H__

#include "backend/metaData.h"

// THE OBJECT TO BASE A NEW ONE ON — the desktop's ibDialogGeneration (frontend/win/dlgs/generation.h), its window the
// client's (frmclient/win/dlgs/generation.h): what the window lists is sent (ibProtocolRequestKind::Generation), what was chosen
// comes back. The form's GenerateForm asks it as the desktop's does.
class ibDialogGeneration {
public:

	bool ShowModal(ibMetaID& id);

	ibDialogGeneration(const ibMetaData* metaData, const ibMetaDescription& metaType);

private:

	const ibMetaData* m_metaData;
	ibMetaDescription m_metaDesc;
};

#endif
