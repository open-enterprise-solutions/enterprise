////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main events
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"
#include "appData.h"

//*********************************************************************************************************
//*                                   Events "moduleManager"                                              *
//*********************************************************************************************************

bool CModuleManager::BeforeStart()
{
	if (appData->EnterpriseMode() || 
		appData->ServiceMode())
	{
		try
		{
			CValue bCancel = false;
			if (m_procUnit != NULL) m_procUnit->CallFunction("beforeStart", bCancel);
			return !bCancel.GetBoolean();
		}
		catch (...)
		{
			return false;
		};
	}; 

	return true;
}

void CModuleManager::OnStart()
{
	if (appData->EnterpriseMode() ||
		appData->ServiceMode())
	{
		try
		{
			if (m_procUnit != NULL) m_procUnit->CallFunction("onStart");
		}
		catch (...)
		{
		};
	}; 
}

bool CModuleManager::BeforeExit()
{
	if (appData->EnterpriseMode() ||
		appData->ServiceMode())
	{
		try
		{
			CValue bCancel = false;
			if (m_procUnit != NULL) m_procUnit->CallFunction("beforeExit", bCancel);
			return !bCancel.GetBoolean();
		}
		catch (...)
		{
			return false;
		};
	};

	return true;
}

void CModuleManager::OnExit()
{
	if (appData->EnterpriseMode() ||
		appData->ServiceMode())
	{
		try
		{
			if (m_procUnit != NULL) m_procUnit->CallFunction("onExit");
		}
		catch (...)
		{
		};
	}; 
}