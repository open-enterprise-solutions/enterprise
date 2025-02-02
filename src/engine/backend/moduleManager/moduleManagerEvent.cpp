////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main events
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"
#include "backend/appData.h"

//*********************************************************************************************************
//*                                   Events "moduleManager"                                              *
//*********************************************************************************************************

bool CModuleManagerConfiguration::BeforeStart()
{
	if (!appData->DesignerMode()) {
		try {
			CValue bCancel = false;
			if (m_procUnit != nullptr) {
				m_procUnit->CallAsProc("beforeStart", bCancel);
			}
			return !bCancel.GetBoolean();
		}
		catch (...)
		{
			return false;
		};
	};

	return true;
}

void CModuleManagerConfiguration::OnStart()
{
	if (!appData->DesignerMode()) {
		try {
			if (m_procUnit != nullptr) {
				m_procUnit->CallAsProc("onStart");
			}
		}
		catch (...) {
		};
	};
}

bool CModuleManagerConfiguration::BeforeExit()
{
	if (!appData->DesignerMode()) {
		try {
			CValue bCancel = false;
			if (m_procUnit != nullptr) {
				m_procUnit->CallAsProc("beforeExit", bCancel);
			}
			return !bCancel.GetBoolean();
		}
		catch (...) {
			return false;
		};
	};

	return true;
}

void CModuleManagerConfiguration::OnExit()
{
	if (!appData->DesignerMode()) {
		try {
			if (m_procUnit != nullptr) {
				m_procUnit->CallAsProc("onExit");
			}
		}
		catch (...) {
		};
	};
}