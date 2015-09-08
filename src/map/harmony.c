// (c) 2008 - 2013 Harmony Project; Daniel Stelter-Gliese / Sirius_White
//  For more information contact info@harmonize.it
//
// This file is NOT public - you are not allowed to distribute it.
#define HERCULES_CORE

#include "../common/cbasetypes.h"
#include "../common/showmsg.h"
#include "../common/db.h"
#include "../common/strlib.h"
#include "../common/mmo.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/sql.h"
#include "../common/console.h"
#include "../common/harmonycore.h"

#ifndef HARMSW
	#define HARMSW HARMSW_EATHENA
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc.h"
#include "clif.h"
#include "atcommand.h"
#include "chrif.h"
#if HARMSW == HARMSW_HERCULES_GROUP
	#include "pc_groups.h"
#endif
#include "npc.h"
#include "harmony.h"

void _FASTCALL harmony_action_request(int fd, int task, int id, intptr data);
void _FASTCALL harmony_log(int fd, const char *msg);
int harmony_group_register_timer(int tid, int64 tick, int id, intptr_t data);

// ----

static SqlStmt* log_stmt = NULL;
static SqlStmt* ban_stmt = NULL;
static SqlStmt* admin_stmt = NULL;

enum {
	LOG_SQL = 1,
	LOG_TXT,
	LOG_TRYSQL
};
static int log_method = LOG_TRYSQL;

static int tid_group_register = INVALID_TIMER;
static int current_groupscan_minlevel = 0;

// ----

void harmony_init() {
	if ( logs->mysql_handle != NULL ) {
		log_stmt = SQL->StmtMalloc(logs->mysql_handle);
		if (SQL_SUCCESS != SQL->StmtPrepareStr(log_stmt, "INSERT DELAYED INTO harmony_log (`account_id`, `char_name`, `IP`, `data`) VALUES (?, ?, ?, ?)")) {
			ShowFatalError("Harmony: Preparing statement 1 failed.\n");
			Sql_ShowDebug(logs->mysql_handle);
			exit(EXIT_FAILURE);
		}
	}

	if (map->mysql_handle == NULL) {
		ShowFatalError("Harmony: SQL is not yet initialized. Please report this!\n");
		exit(EXIT_FAILURE);
	}

	ban_stmt = SQL->StmtMalloc(map->mysql_handle);
	if (SQL_SUCCESS != SQL->StmtPrepareStr(ban_stmt, "UPDATE login SET state = ? WHERE account_id = ?")) {
		ShowFatalError("Harmony: Preparing statement 2 failed.\n");
		Sql_ShowDebug(map->mysql_handle);
		exit(EXIT_FAILURE);
	}

	admin_stmt = SQL->StmtMalloc(map->mysql_handle);
#if HARMSW == HARMSW_HERCULES_GROUP
	if (SQL_SUCCESS != SQL->StmtPrepareStr(admin_stmt, "SELECT account_id FROM `login` WHERE `group_id` = ?")) {
#else
	if (SQL_SUCCESS != SQL->StmtPrepareStr(admin_stmt, "SELECT account_id FROM `login` WHERE `level` >= ?")) {
#endif
		ShowFatalError("Harmony: Preparing statement 3 failed.\n");
		Sql_ShowDebug(map->mysql_handle);
		exit(EXIT_FAILURE);
	}

	ea_funcs->action_request = harmony_action_request;
	ea_funcs->player_log = harmony_log;

	harm_funcs->zone_init();
}

void harmony_final() {
	harm_funcs->zone_final();
	if (log_stmt) {
		SQL->StmtFree(log_stmt);
		log_stmt = NULL;
	}
	SQL->StmtFree(ban_stmt);
	SQL->StmtFree(admin_stmt);
	if (tid_group_register != INVALID_TIMER) {
		timer->delete(tid_group_register, harmony_group_register_timer);
		tid_group_register = INVALID_TIMER;
	}
}

void harmony_parse(int fd,struct map_session_data *sd) {
}

void harmony_ban(int32 account_id, int state) {
	if (!ban_stmt)
		return;
	if (SQL_SUCCESS != SQL->StmtBindParam(ban_stmt, 0, SQLDT_INT, (void*)&state, sizeof(state)) ||
		SQL_SUCCESS != SQL->StmtBindParam(ban_stmt, 1, SQLDT_INT, (void*)&account_id, sizeof(account_id)) ||
		SQL_SUCCESS != SQL->StmtExecute(ban_stmt))
	{
		Sql_ShowDebug(map->mysql_handle);
	}
	ShowMessage(""CL_MAGENTA"[Harmony]"CL_RESET": Banned account #%d (state %d)\n", account_id, state);
}

int harmony_log_sql(TBL_PC* sd, const char* ip, const char* msg) {
	if (!logs->mysql_handle || !log_stmt)
		return 0;
	if (SQL_SUCCESS != SQL->StmtBindParam(log_stmt, 0, SQLDT_INT, (void*)&sd->status.account_id, sizeof(sd->status.account_id)) ||
		SQL_SUCCESS != SQL->StmtBindParam(log_stmt, 1, SQLDT_STRING, (void*)&sd->status.name, strlen(sd->status.name)) ||
		SQL_SUCCESS != SQL->StmtBindParam(log_stmt, 2, SQLDT_STRING, (void*)ip, strlen(ip)) ||
		SQL_SUCCESS != SQL->StmtBindParam(log_stmt, 3, SQLDT_STRING, (void*)msg, strlen(msg)) ||
		SQL_SUCCESS != SQL->StmtExecute(log_stmt))
	{
		Sql_ShowDebug(logs->mysql_handle);
		return 0;
	}
	return 1;
}

int harmony_log_txt(TBL_PC* sd, const char* ip, const char* msg) {
	FILE* logfp;
	static char timestring[255];
	time_t curtime;

	if((logfp = fopen("./log/harmony.log", "a+")) == NULL)
		return 0;
	time(&curtime);
	strftime(timestring, 254, "%m/%d/%Y %H:%M:%S", localtime(&curtime));
	fprintf(logfp,"%s - %s[%d:%d] - %s\t%s\n", timestring, sd->status.name, sd->status.account_id, sd->status.char_id, ip, msg);
	fclose(logfp);
	return 1;
}

void _FASTCALL harmony_log(int fd, const char *msg) {
	char ip[20];
	TBL_PC *sd = (TBL_PC*)session[fd]->session_data;

	if (!sd)
		return;

	sprintf(ip, "%u.%u.%u.%u", CONVIP(session[fd]->client_addr));

	if (log_method == LOG_SQL && !harmony_log_sql(sd, ip, msg)) {
		ShowError("Logging to harmony_log failed. Please check your Harmony setup.\n");
	} else if (log_method == LOG_TRYSQL && log_stmt && harmony_log_sql(sd, ip, msg)) {
		;
	} else if (log_method == LOG_TXT || log_method == LOG_TRYSQL) {
		harmony_log_txt(sd, ip, msg);
	}

	ShowMessage(""CL_MAGENTA"[Harmony]"CL_RESET": %s (Player: %s, IP: %s)\n", msg, sd->status.name, ip);
}

static bool harmony_iterate_groups(int group_id, int level, const char* name) {
	harm_funcs->zone_register_group(group_id, level);
	return true;
}

static void harmony_register_groups() {
	harm_funcs->zone_register_group(25311, 25311);
#if HARMSW == HARMSW_HERCULES_GROUP
		pc_group_iterate(harmony_iterate_groups);
#else
		harm_funcs->zone_register_group(25312, 25312);
#endif
}

int harmony_group_register_timer(int tid, int64 tick, int id, intptr_t data) {
	if (chrif->isconnected()) {
		harmony_register_groups();
		
		// We will re-send group associations every thirty seconds, otherwise the login server would never be able to recover from a restart
		timer->delete(tid, harmony_group_register_timer);
		tid_group_register = timer->add_interval(timer->gettick()+30*1000, harmony_group_register_timer, 0, 0, 30*1000);
	}
	return 0;
}

static bool harmony_iterate_groups_adminlevel(int group_id, int level, const char* name) {
	int account_id;

	if (level < current_groupscan_minlevel)
		return true;

	ShowInfo("Registering group %d..\n", group_id);
	if (SQL_SUCCESS != SQL->StmtBindParam(admin_stmt, 0, SQLDT_INT, (void*)&group_id, sizeof(group_id)) ||
		SQL_SUCCESS != SQL->StmtExecute(admin_stmt))
	{
		ShowError("Fetching GM accounts from group %d failed.\n", group_id);
		Sql_ShowDebug(map->mysql_handle);
		return true;
	}

	SQL->StmtBindColumn(admin_stmt, 0, SQLDT_INT, &account_id, 0, NULL, NULL);
	while (SQL_SUCCESS == SQL->StmtNextRow(admin_stmt)) {
		harm_funcs->zone_register_admin(account_id, false);
	}
	return true;
}

void harmony_action_request_global(int task, int id, intptr data) {
	switch (task) {
	case HARMTASK_LOGIN_ACTION:
		chrif_harmony_request((uint8*)data, id);
		break;
	case HARMTASK_GET_FD:
		{
		TBL_PC *sd = BL_CAST(BL_PC, map->id2bl(id));
		*(int32*)data = (sd ? sd->fd : 0);
		}
		break;
	case HARMTASK_SET_LOG_METHOD:
		log_method = id;
		break;
	case HARMTASK_INIT_GROUPS:
		if (chrif->isconnected())
			harmony_register_groups();
		else {
			// Register groups as soon as the char server is available again
			if (tid_group_register != INVALID_TIMER)
				timer->delete(tid_group_register, harmony_group_register_timer);
			tid_group_register = timer->add_interval(timer->gettick()+1000, harmony_group_register_timer, 0, 0, 500);
		}
		break;
	case HARMTASK_RESOLVE_GROUP:
#if HARMSW == HARMSW_HERCULES_GROUP
		*(int32*)data = pcg->id2group(id)->level;
#else
		*(int32*)data = id;
#endif
		break;
	case HARMTASK_PACKET:
		clif->send((const uint8*)data, id, NULL, ALL_CLIENT);
		break;
	case HARMTASK_GET_ADMINS:
	{
#if HARMSW == HARMSW_HERCULES_GROUP
		// Iterate groups and register each group individually
		current_groupscan_minlevel = id;
		pc_group_iterate(harmony_iterate_groups_adminlevel);
#else
		//
		int account_id;
		int level = id;
		if (SQL_SUCCESS != SQL->StmtBindParam(admin_stmt, 0, SQLDT_INT, (void*)&level, sizeof(level)) ||
			SQL_SUCCESS != SQL->StmtExecute(admin_stmt))
		{
			ShowError("Fetching GM accounts failed.\n");
			Sql_ShowDebug(map->mysql_handle);
			break;
		}

		SQL->StmtBindColumn(admin_stmt, 0, SQLDT_INT, &account_id, 0, NULL, NULL);
		while (SQL_SUCCESS == SQL->StmtNextRow(admin_stmt)) {
			harm_funcs->zone_register_admin(account_id, false);
		}
#endif
		break;
	}
	case HARMTASK_IS_CHAR_CONNECTED:
		*(int*)data = chrif->isconnected();
		break;
	default:
		ShowError("Harmony requested unknown action! (Global; ID=%d)\n", task);
		ShowError("This indicates that you are running an incompatible version.\n");
		break;
	}
}

void _FASTCALL harmony_action_request(int fd, int task, int id, intptr data) {
	TBL_PC *sd;

	if (fd == 0) {
		harmony_action_request_global(task, id, data);
		return;
	}

	switch (task) {
	case HARMTASK_PACKET:
		memcpy(WFIFOP(fd, 0), (const void*)data, id);
		//ShowInfo("Sending %d bytes to session #%d (%x)\n", id, fd, WFIFOW(fd, 0));
		WFIFOSET(fd, id);
		return;
	}

	sd = (TBL_PC *)session[fd]->session_data;
	if (!sd)
		return;

	switch (task) {
	case HARMTASK_DC:
		ShowInfo("-- Harmony requested disconnect.\n");
		set_eof(fd);
		break;
	case HARMTASK_KICK:
		ShowInfo("-- Harmony requested kick.\n");
		if (id == 99)
			set_eof(fd);
		else
			clif->authfail_fd(fd, id);
		break;
	case HARMTASK_JAIL:
	{
		char msg[64];
		snprintf(msg, sizeof(msg)-1, "@jail %s", sd->status.name);
		atcommand->exec(0, sd, msg, 0);
	}
		break;
	case HARMTASK_BAN:
		harmony_ban(sd->status.account_id, id);
		break;
	case HARMTASK_ATCMD:
		atcommand->exec(fd, sd, (const char*)data, 0);
		break;
	case HARMTASK_MSG:
		clif->message(fd, (const char*)data);
		break;
	case HARMTASK_IS_ACTIVE:
		*(int32*)data = (sd->bl.prev == NULL || sd->invincible_timer != INVALID_TIMER) ? 0 : 1;
		break;
	case HARMTASK_GET_ID:
		switch (id) {
			case HARMID_AID:
				*(int*)data = sd->status.account_id;
				break;
			case HARMID_GID:
				*(int*)data = sd->status.char_id;
				break;
			case HARMID_GDID:
				*(int*)data = sd->status.guild_id;
				break;
			case HARMID_PID:
				*(int*)data = sd->status.party_id;
				break;
			case HARMID_CLASS:
				*(short*)data = sd->status.class_;
				break;
			case HARMID_GM:
	#if HARMSW == HARMSW_HERCULES_GROUP
				*(int*)data = pcg->id2group(sd->group_id)->level;
	#else
				*(int*)data = pc_isGM(sd);
	#endif
				break;
			default:
				ShowError("Harmony requested unknown ID! (ID=%d)\n", id);
				ShowError("This indicates that you are running an incompatible version.\n");
				break;
		}
		break;
	case HARMTASK_SCRIPT:
		{
			struct npc_data* nd = npc->name2id((const char*)data);
			if (nd) {
				script->run(nd->u.scr.script, 0, sd->bl.id, npc->fake_nd->bl.id);
			} else {
				ShowError("A Harmony action chain tried to execute non-existing script '%s'\n", (const char*)data);
			}
		}
		break;
	default:
		ShowError("Harmony requested unknown action! (ID=%d)\n", task);
		ShowError("This indicates that you are running an incompatible version.\n");
		break;
	}
}

void harmony_logout(TBL_PC* sd) {
	harm_funcs->zone_logout(sd->fd);
}


