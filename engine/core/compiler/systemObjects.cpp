////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : system objects 
////////////////////////////////////////////////////////////////////////////

#include "systemObjects.h"

wxDateTime CSystemObjects::m_workDate = wxDateTime::Now();

class CAutoInit { public: CAutoInit() { srand((unsigned)time(NULL)); }; } m_autoInit;

CSystemObjects::CSystemObjects() : CValue(eValueTypes::TYPE_VALUE, true) {}