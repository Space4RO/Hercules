#include "../common/HPMi.h"
#include "map/script.h"
#include "map/mob.h"
#include "map/mapreg.h"
#include "../common/HPMDataCheck.h"


#define MAX_MOB_SPAWN_SEARCH 100


BUILDIN(mobspawn_getdata) {
	int i, j, cdx = 0, count = 0;
	char buf[MESSAGE_SIZE];
	struct mob_db *mob_array[MAX_MOB_SPAWN_SEARCH];
	struct map_session_data *sd = script->rid2sd(st);
	int type = script_getnum(st, 2);
	const char *str = script_getstr(st, 3);

	if (sd == NULL)
		return true;

	if (type == 1) {
		if ((i = mob->db_checkid(atoi(str))) > 0) {
			mob_array[0] = mob->db(i);
			count = 1;
		}
		else {
			count = mob->db_searchname_array(mob_array, MAX_MOB_SPAWN_SEARCH, str, 0);

			if (count > MAX_MOB_SPAWN_SEARCH)
				count = MAX_MOB_SPAWN_SEARCH;
		}

		for (i = 0; i < count; i++) {
			if (mob_array[i]->spawn[0].mapindex) {
				mapreg->setreg(reference_uid(script->add_str("$@mobid"), cdx), mob_array[i]->vd.class_);
				cdx++;
			}
		}

		mapreg->setreg(script->add_str("$@mobcount"), cdx);
	}
	else if (type == 2) {
		for (i = 0; i < MAX_MOB_DB; i++) {
			if (mob->db_data[i] == NULL)
				continue;

			if (mob->db_data[i]->spawn[0].mapindex == 0)
				continue;

			ARR_FIND(0, MAX_MOB_DROP, j, mob->db_data[i]->dropitem[j].nameid == atoi(str));
			if (j < MAX_MOB_DROP) {
				mapreg->setreg(reference_uid(script->add_str("$@mobid"), cdx), mob->db_data[i]->vd.class_);
				cdx++;
			}
		}

		mapreg->setreg(script->add_str("$@mobcount"), cdx);
	}
	else if (type == 3) {
		for (i = 0; i < map->count; i++) {
			count = 0;
			ARR_FIND(0, MAX_MOB_LIST_PER_MAP, j, map->list[i].moblist[j] && map->list[i].moblist[j]->class_ == atoi(str));
			if (j < MAX_MOB_LIST_PER_MAP) {
				count += map->list[i].moblist[j]->num;
				sprintf(buf, "%s:%d", map->list[i].name, count);
				mapreg->setregstr(reference_uid(script->add_str("$@mobspawn$"), cdx), buf);
				cdx++;
			}
		}

		mapreg->setreg(script->add_str("$@mobcount"), cdx);
	}

	return true;
}


HPExport struct hplugin_info pinfo = {
	"UserCP",
	SERVER_TYPE_MAP,
	"1.0",
	HPM_VERSION,
};

HPExport void plugin_init(void) {
	script = GET_SYMBOL("script");
	mob = GET_SYMBOL("mob");
	mapreg = GET_SYMBOL("mapreg");

	addScriptCommand("mobspawn_getdata", "is", mobspawn_getdata);
}