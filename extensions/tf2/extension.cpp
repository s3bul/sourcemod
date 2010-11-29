/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Team Fortress 2 Extension
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include <sourcemod_version.h>
#include "extension.h"
#include "util.h"
#include "RegNatives.h"
#include "iplayerinfo.h"
#include "sm_trie_tpl.h"
#include "criticals.h"
#include "holiday.h"
#include "CDetour/detours.h"

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */


TF2Tools g_TF2Tools;
IGameConfig *g_pGameConf = NULL;

IBinTools *g_pBinTools = NULL;

SMEXT_LINK(&g_TF2Tools);

ICvar *icvar = NULL;

CGlobalVars *gpGlobals = NULL;

sm_sendprop_info_t *playerSharedOffset;

extern sp_nativeinfo_t g_TFNatives[];

int g_resourceEntity;

SH_DECL_HOOK3_void(IServerGameDLL, ServerActivate, SH_NOATTRIB, 0, edict_t *, int , int);

bool TF2Tools::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	if (strcmp(g_pSM->GetGameFolderName(), "tf") != 0)
	{
		UTIL_Format(error, maxlength, "Cannot Load TF2 Extension on mods other than TF2");
		return false;
	}

	ServerClass *sc = UTIL_FindServerClass("CTFPlayer");
	if (sc == NULL)
	{
		UTIL_Format(error, maxlength, "Could not find CTFPlayer server class");
		return false;
	}

	playerSharedOffset = new sm_sendprop_info_t;

	if (!UTIL_FindDataTable(sc->m_pTable, "DT_TFPlayerShared", playerSharedOffset, 0))
	{
		UTIL_Format(error, maxlength, "Could not find DT_TFPlayerShared data table");
		return false;
	}

	sharesys->AddDependency(myself, "bintools.ext", true, true);

	char conf_error[255] = "";
	if (!gameconfs->LoadGameConfigFile("sm-tf2.games", &g_pGameConf, conf_error, sizeof(conf_error)))
	{
		if (conf_error[0])
		{
			UTIL_Format(error, maxlength, "Could not read sm-tf2.games.txt: %s", conf_error);
		}
		return false;
	}

	CDetourManager::Init(g_pSM->GetScriptingEngine(), g_pGameConf);

	sharesys->AddNatives(myself, g_TFNatives);
	sharesys->RegisterLibrary(myself, "tf2");

	plsys->AddPluginsListener(this);

	playerhelpers->RegisterCommandTargetProcessor(this);

	g_critForward = forwards->CreateForward("TF2_CalcIsAttackCritical", ET_Hook, 4, NULL, Param_Cell, Param_Cell, Param_String, Param_CellByRef);
	g_getHolidayForward = forwards->CreateForward("TF2_OnGetHoliday", ET_Event, 1, NULL, Param_CellByRef);

	g_pCVar = icvar;

	m_CritDetoursEnabled = false;
	m_GetHolidayDetourEnabled = false;

	return true;
}

const char *TF2Tools::GetExtensionVerString()
{
	return SM_FULL_VERSION;
}

const char *TF2Tools::GetExtensionDateString()
{
	return SM_BUILD_TIMESTAMP;
}

bool TF2Tools::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);

	gpGlobals = ismm->GetCGlobals();

	SH_ADD_HOOK(IServerGameDLL, ServerActivate, gamedll, SH_STATIC(OnServerActivate), true);

	GET_V_IFACE_CURRENT(GetEngineFactory, icvar, ICvar, CVAR_INTERFACE_VERSION);

	GET_V_IFACE_CURRENT(GetServerFactory, gameents, IServerGameEnts, INTERFACEVERSION_SERVERGAMEENTS);

	GET_V_IFACE_CURRENT(GetEngineFactory, m_GameEventManager, IGameEventManager2, INTERFACEVERSION_GAMEEVENTSMANAGER2);
	m_GameEventManager->AddListener(this, "teamplay_restart_round", true);

	return true;
}

void TF2Tools::SDK_OnUnload()
{
	SH_REMOVE_HOOK(IServerGameDLL, ServerActivate, gamedll, SH_STATIC(OnServerActivate), true);

	g_RegNatives.UnregisterAll();
	gameconfs->CloseGameConfigFile(g_pGameConf);
	playerhelpers->UnregisterCommandTargetProcessor(this);

	plsys->RemovePluginsListener(this);

	forwards->ReleaseForward(g_critForward);
	forwards->ReleaseForward(g_getHolidayForward);
}

void TF2Tools::SDK_OnAllLoaded()
{
	SM_GET_LATE_IFACE(BINTOOLS, g_pBinTools);
}

bool TF2Tools::RegisterConCommandBase(ConCommandBase *pVar)
{
	return g_SMAPI->RegisterConCommandBase(g_PLAPI, pVar);
}

void TF2Tools::FireGameEvent( IGameEvent *event )
{
	timersys->NotifyOfGameStart();
	timersys->MapTimeLeftChanged();
}

bool TF2Tools::QueryRunning(char *error, size_t maxlength)
{
	SM_CHECK_IFACE(BINTOOLS, g_pBinTools);

	return true;
}

bool TF2Tools::QueryInterfaceDrop(SMInterface *pInterface)
{
	if (pInterface == g_pBinTools)
	{
		return false;
	}

	return IExtensionInterface::QueryInterfaceDrop(pInterface);
}

void TF2Tools::NotifyInterfaceDrop(SMInterface *pInterface)
{
	g_RegNatives.UnregisterAll();
}

void OnServerActivate(edict_t *pEdictList, int edictCount, int clientMax)
{
	g_resourceEntity = FindResourceEntity();
}

bool TF2Tools::ProcessCommandTarget(cmd_target_info_t *info)
{
	int max_clients;
	IPlayerInfo *pInfo;
	unsigned int team_index = 0;
	IGamePlayer *pPlayer, *pAdmin;

	if ((info->flags & COMMAND_FILTER_NO_MULTI) == COMMAND_FILTER_NO_MULTI)
	{
		return false;
	}

	if (info->admin)
	{
		if ((pAdmin = playerhelpers->GetGamePlayer(info->admin)) == NULL)
		{
			return false;
		}
		if (!pAdmin->IsInGame())
		{
			return false;
		}
	}
	else
	{
		pAdmin = NULL;
	}

	if (strcmp(info->pattern, "@red") == 0 )
	{
		team_index = 2;
	}
	else if (strcmp(info->pattern, "@blue") == 0)
	{
		team_index = 3;
	}
	else
	{
		return false;
	}

	info->num_targets = 0;

	max_clients = playerhelpers->GetMaxClients();
	for (int i = 1; 
		 i <= max_clients && (cell_t)info->num_targets < info->max_targets; 
		 i++)
	{
		if ((pPlayer = playerhelpers->GetGamePlayer(i)) == NULL)
		{
			continue;
		}
		if (!pPlayer->IsInGame())
		{
			continue;
		}
		if ((pInfo = pPlayer->GetPlayerInfo()) == NULL)
		{
			continue;
		}
		if (pInfo->GetTeamIndex() != (int)team_index)
		{
			continue;
		}
		if (playerhelpers->FilterCommandTarget(pAdmin, pPlayer, info->flags) 
			!= COMMAND_TARGET_VALID)
		{
			continue;
		}
		info->targets[info->num_targets] = i;
		info->num_targets++;
	}

	if (info->num_targets == 0)
	{
		info->reason = COMMAND_TARGET_EMPTY_FILTER;
	}
	else
	{
		info->reason = COMMAND_TARGET_VALID;
	}

	info->target_name_style = COMMAND_TARGETNAME_RAW;
	if (team_index == 2)
	{
		UTIL_Format(info->target_name, info->target_name_maxlength, "Red Team");
	}
	else if (team_index == 3)
	{
		UTIL_Format(info->target_name, info->target_name_maxlength, "Blue Team");
	}

	return true;
}

void TF2Tools::OnPluginLoaded(IPlugin *plugin)
{
	if (!m_CritDetoursEnabled && g_critForward->GetFunctionCount())
	{
		m_CritDetoursEnabled = InitialiseCritDetours();
	}
	if (!m_GetHolidayDetourEnabled && g_getHolidayForward->GetFunctionCount())
	{
		m_GetHolidayDetourEnabled = InitialiseGetHolidayDetour();
	}
}

void TF2Tools::OnPluginUnloaded(IPlugin *plugin)
{
	if (m_CritDetoursEnabled && !g_critForward->GetFunctionCount())
	{
		RemoveCritDetours();
		m_CritDetoursEnabled = false;
	}
	if (m_GetHolidayDetourEnabled && !g_getHolidayForward->GetFunctionCount())
	{
		RemoveGetHolidayDetour();
		m_GetHolidayDetourEnabled = false;
	}
}
int FindResourceEntity()
{
	return FindEntityByNetClass(-1, "CTFPlayerResource");
}


int FindEntityByNetClass(int start, const char *classname)
{
	edict_t *current;

	for (int i = ((start != -1) ? start : 0); i < gpGlobals->maxEntities; i++)
	{
		current = engine->PEntityOfEntIndex(i);
		if (current == NULL)
		{
			continue;
		}

		IServerNetworkable *network = current->GetNetworkable();

		if (network == NULL)
		{
			continue;
		}

		ServerClass *sClass = network->GetServerClass();
		const char *name = sClass->GetName();
		

		if (strcmp(name, classname) == 0)
		{
			return i;
		}
	}

	return -1;
}

TFClassType ClassnameToType(const char *classname)
{
	static KTrie<TFClassType> trie;
	static bool filled = false;
	
	if (!filled)
	{
		trie.insert("scout", TFClass_Scout);
		trie.insert("sniper", TFClass_Sniper);
		trie.insert("soldier", TFClass_Soldier);
		trie.insert("demoman", TFClass_DemoMan);
		trie.insert("demo", TFClass_DemoMan);
		trie.insert("medic", TFClass_Medic);
		trie.insert("heavy", TFClass_Heavy);
		trie.insert("hwg", TFClass_Heavy);
		trie.insert("pyro", TFClass_Pyro);
		trie.insert("spy", TFClass_Spy);
		trie.insert("engineer", TFClass_Engineer);
		trie.insert("engy", TFClass_Engineer);

		filled = true;	
	}
	
	TFClassType *value;

	if (!(value = trie.retrieve(classname)))
	{
		return TFClass_Unknown;	
	}

	return *value;
}



/**
 * A picture of a blue crab given to me as a gift and stored here for safe keeping
 *
 * http://www.democracycellproject.net/blog/archives/Clown%20car.jpg
 */

