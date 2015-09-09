#include "common/HPMi.h"
#include "common/socket.h"
#include "common/mapindex.h"

#include "char/char.h"

#undef DEFAULT_AUTOSAVE_INTERVAL
#include "map/clif.h"
#include "map/chrif.h"
#include "map/itemdb.h"
#include "map/map.h"
#include "map/pc.h"
#include "map/atcommand.h"

#include "common/HPMDataCheck.h"

#include <stdlib.h>

struct admin_cp {
	char *server_type;
	char *server_name;

	int fake_users[MAX_MAP_SERVERS];
};


struct admin_cp admincp;


int char_count_users(void) {
	int i, users = 0;
	for (i = 0; i < ARRAYLENGTH(chr->server); i++) {
		if (chr->server[i].fd > 0) {
			users += chr->server[i].users;
			users += admincp.fake_users[i];
		}
	}
	return users;
}

void char_frommap_set_users_count(int fd, int id) {
	if (RFIFOW(fd, 2) != chr->server[id].users || RFIFOW(fd, 4) != admincp.fake_users[id]) {
		chr->server[id].users = RFIFOW(fd, 2);
		admincp.fake_users[id] = RFIFOW(fd, 4);
		ShowInfo("User Count: %d Fake: %d (Server: %d)\n", chr->server[id].users, admincp.fake_users[id], id);
	}
	RFIFOSKIP(fd, 6);
}


bool hookPost_pc_authok(int retVal, struct map_session_data *sd, int *login_id2, time_t *expiration_time, int *group_id, struct mmo_charstatus *st, bool *changing_mapservers) {
	admincp.fake_users[0] += rand() % 3;
	return retVal;
}

int hookPost_map_quit(int retVal, struct map_session_data *sd) {
	admincp.fake_users[0] -= rand() % 3;
	if (admincp.fake_users[0] < 0)
		admincp.fake_users[0] = 0;
	return retVal;
}

#define chrif_check(a) do { if(!chrif->isconnected()) return a; } while(0)
int chrif_send_usercount_tochar(int tid, int64 tick, int id, intptr_t data) {
	chrif_check(-1);

	WFIFOHEAD(chrif->fd, 4);
	WFIFOW(chrif->fd, 0) = 0x2afe;
	WFIFOW(chrif->fd, 2) = map->usercount();
	WFIFOW(chrif->fd, 4) = admincp.fake_users[0];
	WFIFOSET(chrif->fd, 6);
	return 0;
}

/**
* Fake players online in game.
* Usage: @fakeuser <number>
*/
ACMD(fakeuser) {
	int num = 0;

	if (!message || !*message || sscanf(message, "%d", &num) < 1) {
		clif->message(fd, "Usage: @fakeuser <number>.");
		return false;
	}

	admincp.fake_users[0] += num;
	if (admincp.fake_users[0] < 0)
		admincp.fake_users[0] = 0;

	return true;
}

/**
* Usage: @recallmap <mapname>
*/
ACMD(recallmap) {
	struct s_mapiterator *iter = NULL;
	struct map_session_data *pl_sd = NULL;
	char map_name[MAP_NAME_LENGTH];
	unsigned short map_index;
	int16 m = -1;

	if (!message || !*message || sscanf(message, "%11s", map_name) < 1) {
		clif->message(fd, "Please enter a map(usage: @recallmap <mapname>.");
		return false;
	}

	map_index = mapindex->name2id(map_name);
	if (!map_index) {
		clif->message(fd, msg_fd(fd, 1));
		return false;
	}

	m = map->mapindex2mapid(map_index);
	iter = mapit_getallusers();
	for (pl_sd = (TBL_PC *)mapit->first(iter); mapit->exists(iter); pl_sd = (TBL_PC *)mapit->next(iter)) {
		if (pl_sd->status.char_id == sd->status.char_id)
			continue;

		if (pl_sd->bl.m != m)
			continue;

		if (pc_isdead(pl_sd) || pc_istrading(pl_sd))
			continue;

		pc->setpos(pl_sd, sd->mapindex, sd->bl.x, sd->bl.y, CLR_RESPAWN);
	}
	mapit->free(iter);

	return true;
}

/**
* A way of giving items to all players on a whole map
* Usage: @itemmap <map_name> <item_name or ID> [quantity]
**/
ACMD(itemmap) {
	struct s_mapiterator *iter = NULL;
	struct map_session_data *pl_sd = NULL;
	struct item item_tmp;
	struct item_data *id;
	char map_name[MAP_NAME_LENGTH];
	char item_name[ITEM_NAME_LENGTH];
	int num = 0, flag = 0;
	unsigned short map_index;
	int16 m = -1;

	if (!message || !*message
		|| (sscanf(message, "%15s \"%99[^\"]\" %d", map_name, item_name, &num) != 3
		&& sscanf(message, "%15s %99s %d", map_name, item_name, &num) != 3)) {
		clif->message(fd, "Usage: @itemmap <map_name> <item_name or ID> [quantity]");
		return false;
	}

	if (num <= 0)
		num = 1;

	if ((id = itemdb->search_name(item_name)) == NULL &&
		(id = itemdb->exists(atoi(item_name))) == NULL) {
		clif->message(fd, msg_fd(fd, 1));
		return false;
	}

	map_index = mapindex->name2id(map_name);
	if (!map_index) {
		clif->message(fd, msg_fd(fd, 1));
		return false;
	}

	if (!itemdb->isstackable(id->nameid))
		num = 1;

	m = map->mapindex2mapid(map_index);
	iter = mapit_getallusers();
	for (pl_sd = (TBL_PC *)mapit->first(iter); mapit->exists(iter); pl_sd = (TBL_PC *)mapit->next(iter)) {
		if (pl_sd->state.autotrade)
			continue;

		if (pc_can_give_items(pl_sd)) {
			memset(&item_tmp, 0, sizeof(item_tmp));
			item_tmp.nameid = id->nameid;
			item_tmp.identify = 1;

			if ((flag = pc->additem(pl_sd, &item_tmp, num, LOG_TYPE_COMMAND))) {
				clif->additem(pl_sd, 0, 0, flag);
			}

			if (flag == 0) {
				clif->message(pl_sd->fd, msg_fd(fd, 18));
			}
		}
	}
	mapit->free(iter);

	return true;
}

HPExport struct hplugin_info pinfo = {
	"AdminCP",
	SERVER_TYPE_ALL,
	"1.0",
	HPM_VERSION,
};


HPExport void plugin_init(void) {
	char *server_type = GET_SYMBOL("SERVER_TYPE");

	sockt = GET_SYMBOL("sockt");
	mapindex = GET_SYMBOL("mapindex");

	session = GET_SYMBOL("session");

	switch (*server_type) {
	case SERVER_TYPE_CHAR:
		chr = GET_SYMBOL("chr");

		chr->count_users = &char_count_users;
		chr->parse_frommap_set_users_count = &char_frommap_set_users_count;
		break;
	case SERVER_TYPE_MAP:
		clif = GET_SYMBOL("clif");
		chrif = GET_SYMBOL("chrif");
		itemdb = GET_SYMBOL("itemdb");
		map = GET_SYMBOL("map");
		mapit = GET_SYMBOL("mapit");
		pc = GET_SYMBOL("pc");
		atcommand = GET_SYMBOL("atcommand");

		addAtcommand("fakeuser", fakeuser);
		addAtcommand("recallmap", recallmap);
		addAtcommand("itemmap", itemmap);

		addHookPost("pc->authok", hookPost_pc_authok);
		addHookPost("map->quit", hookPost_map_quit);

		chrif->packet_len_table[6] = 8;
		chrif->send_usercount_tochar = &chrif_send_usercount_tochar;
		break;
	}
}


HPExport void server_online(void) {
	char *server_type = GET_SYMBOL("SERVER_TYPE");

	switch (*server_type) {
	case SERVER_TYPE_MAP:
		admincp.fake_users[0] = 0;
		break;
	}
}
