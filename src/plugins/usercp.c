#include "common/HPMi.h"
#include "common/db.h"
#include "map/script.h"
#include "map/mob.h"
#include "map/mapreg.h"
#include "map/vending.h"
#include "map/searchstore.h"
#include "map/itemdb.h"
#include "map/pc.h"
#include "../common/HPMDataCheck.h"


#define MAX_MOBSPAWN_SEARCH		0x0100
#define MAX_SEARCHSTORE_SEARCH	0x1000


static int itemdb_searchname_array(struct item_data** data, int size, const char *prefix, int type, int class1, int class2, int loc);


BUILDIN(mobspawn_getdata) {
	int i, j, cdx = 0, count = 0;
	char buf[MESSAGE_SIZE];
	struct mob_db *mob_array[MAX_MOBSPAWN_SEARCH];
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
			count = mob->db_searchname_array(mob_array, MAX_MOBSPAWN_SEARCH, str, 0);

			if (count > MAX_MOBSPAWN_SEARCH)
				count = MAX_MOBSPAWN_SEARCH;
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

BUILDIN(searchstores_query) {
	int idx;
	struct DBIterator *iter;
	struct map_session_data *pl_sd = NULL;
	struct map_session_data *sd = script->rid2sd(st);
	int nameid = script_getnum(st, 2);
	int flag = script_getnum(st, 3);
	bool found = false;

	if (!sd)
		return true;


	if (!flag) {
		searchstore->open(sd, 1, 1);
		searchstore->query(sd, 0, 0, 0, (const unsigned short*)&nameid, 1, NULL, 0);
	}
	else {
		iter = db_iterator(vending->db);
		for (pl_sd = dbi_first(iter); dbi_exists(iter); pl_sd = dbi_next(iter)) {
			ARR_FIND(0, pl_sd->vend_num, idx, pl_sd->status.cart[pl_sd->vending[idx].index].nameid == nameid);
			if (idx < pl_sd->vend_num) {
				found = true;
				break;
			}
		}
		dbi_destroy(iter);
	}

	script_pushint(st, found);
	return true;
}

BUILDIN(searchstores_getdata) {
	struct DBIterator *iter;
	struct map_session_data *pl_sd = NULL;
	struct item_data *item_array[MAX_SEARCHSTORE_SEARCH];
	const char *letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	char prefix[2], str[256];
	int i, idx, count = 0, num = 0;
	int type = script_getnum(st, 2);
	int class1 = script_getnum(st, 3);
	int class2 = script_getnum(st, 4);
	int loc = script_getnum(st, 5);

	iter = db_iterator(vending->db);
	while (*letters != 0) {
		prefix[0] = *letters;
		prefix[1] = 0;
		count = itemdb_searchname_array(item_array, MAX_SEARCHSTORE_SEARCH, prefix, type, class1, class2, loc);
		for (i = 0; i < count && i < MAX_SEARCHSTORE_SEARCH; i++) {
			for (pl_sd = dbi_first(iter); dbi_exists(iter); pl_sd = dbi_next(iter)) {
				ARR_FIND(0, pl_sd->vend_num, idx, pl_sd->status.cart[pl_sd->vending[idx].index].nameid == item_array[i]->nameid);
				if (idx < pl_sd->vend_num) {
					*prefix = TOLOWER(prefix[0]);
					sprintf(str, "$@searchstore_prefix%s_num", prefix);
					num = mapreg->readreg(script->add_str(str));
					mapreg->setreg(script->add_str(str), num + 1);
					sprintf(str, "$@searchstore_prefix%s_nameid", prefix);
					mapreg->setreg(reference_uid(script->add_str(str), num), item_array[i]->nameid);
					break;
				}
			}
		}

		letters++;
	}
	dbi_destroy(iter);

	return true;
}


HPExport struct hplugin_info pinfo = {
	"UserCP",
	SERVER_TYPE_MAP,
	"2.0",
	HPM_VERSION,
};

HPExport void plugin_init(void) {
	DB = GET_SYMBOL("DB");
	script = GET_SYMBOL("script");
	mob = GET_SYMBOL("mob");
	mapreg = GET_SYMBOL("mapreg");
	vending = GET_SYMBOL("vending");
	searchstore = GET_SYMBOL("searchstore");
	itemdb = GET_SYMBOL("itemdb");

	addScriptCommand("mobspawn_getdata", "is", mobspawn_getdata);

	addScriptCommand("searchstores_query", "ii", searchstores_query);
	addScriptCommand("searchstores_getdata", "iiii", searchstores_getdata);
}


int itemdb_searchname_array(struct item_data** data, int size, const char *prefix, int type, int class1, int class2, int loc) {
	struct item_data *item;
	int i, count = 0;

	for (i = 0; i < ARRAYLENGTH(itemdb->array); ++i) {
		if ((item = itemdb->array[i]) == NULL)
			continue;

		if (item->jname[0] == *prefix) {
			if ((item->type == type && type == IT_WEAPON) &&
				(item->look >= class1 && item->look <= class2)) {
				if (count < size)
					data[count] = item;
				++count;
			}
			if ((item->type == type && type == IT_ARMOR) &&
				(item->equip&loc)) {
				if (count < size)
					data[count] = item;
				++count;
			}
			else if (item->type == type && type == IT_PETARMOR) {
				if (count < size)
					data[count] = item;
				++count;
			}
			else if (item->type == type &&
				(type == IT_HEALING || type == IT_USABLE || type == IT_ETC || type == IT_CARD ||
				type == IT_PETEGG || type == IT_AMMO || type == IT_DELAYCONSUME || type == IT_CASH))
			{
				if (count < size)
					data[count] = item;
				++count;
			}
		}
		else if (item->type == type && (type == IT_ARMOR) &&
			((item->equip&loc) && (loc&EQP_COSTUME))) {
			if (item->jname[0] == '[' && item->jname[2] == ']' && item->jname[4] == *prefix) {
				if (count < size)
					data[count] = item;
				++count;
			}
			else if (item->jname[0] == *prefix) {
				if (count < size)
					data[count] = item;
				++count;
			}
		}
	}

	return count;
}