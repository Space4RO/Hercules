#include "common/HPMi.h"
#include "common/timer.h"

#include "map/clif.h"
#include "map/map.h"
#include "map/unit.h"
#include "map/battle.h"
#include "map/pc.h"
#include "map/status.h"
#include "map/skill.h"
#include "map/itemdb.h"

#include "common/HPMDataCheck.h"

int hookPre_skill_cell_overlap(struct block_list *bl, va_list ap) {
	struct skill_unit *su;
	uint16 skill_id = va_arg(ap, int);
	int *alive = va_arg(ap, int *);

	su = (struct skill_unit *)bl;

	if (su && su->group && (*alive) != 0) {
		if (skill_id == SA_LANDPROTECTOR) {
			if (su->group->skill_id == SA_LANDPROTECTOR) {
				(*alive) = 0;
				skill->delunit(su);
				return 1;
			}
			if (!(skill->get_inf2(su->group->skill_id)&(INF2_SONG_DANCE | INF2_TRAP | INF2_NOLP)) || su->group->skill_id == GN_HELLS_PLANT) {
				skill->delunit(su);
				return 1;
			}
			hookStop();
		}
	}
	return 0;
}

int hookPre_skill_castend_pos2(struct block_list *src, int *x, int *y, uint16 *skill_id, uint16 *skill_lv, int64 *tick, int *flag) {
	if (src && src->type == BL_PC && *skill_id == MO_BODYRELOCATION) {
		struct map_session_data *sd = BL_CAST(BL_PC, src);
		if (sd && sd->sc.data[SC_SPIDERWEB]) {
			clif->skill_fail(sd, *skill_id, USESKILL_FAIL_LEVEL, 0);
			hookStop();
		}
	}
	return 0;
}

int hookPre_skill_castend_damage_id(struct block_list *src, struct block_list *bl, uint16 *skill_id, uint16 *skill_lv, int64 *tick, int *flag) {
	if (src && src->type == BL_PC && *skill_id == MO_EXTREMITYFIST) {
		short x, y, i = 2; // Move 2 cells for Issen(from target)
		struct block_list *mbl = bl;
		short dir = 0;

		skill->attack(BF_WEAPON, src, src, bl, *skill_id, *skill_lv, *tick, *flag);

		if (*skill_id == MO_EXTREMITYFIST) {
			mbl = src;
			i = 3; // for Asura(from caster)
			status->set_sp(src, 0, 0);
			status_change_end(src, SC_EXPLOSIONSPIRITS, INVALID_TIMER);
			status_change_end(src, SC_BLADESTOP, INVALID_TIMER);
		}
		else {
			status_change_end(src, SC_NJ_NEN, INVALID_TIMER);
			status_change_end(src, SC_HIDING, INVALID_TIMER);
		}

		dir = map->calc_dir(src, bl->x, bl->y);
		if (dir > 0 && dir < 4) x = -i;
		else if (dir > 4) x = i;
		else x = 0;
		if (dir > 2 && dir < 6) y = -i;
		else if (dir == 7 || dir < 2) y = i;
		else y = 0;
		if ((mbl == src || (!map_flag_gvg2(src->m) && !map->list[src->m].flag.battleground))) { // only NJ_ISSEN don't have slide effect in GVG
			if (!(unit->movepos(src, mbl->x + x, mbl->y + y, 1, 1))) {
				// The cell is not reachable (wall, object, ...), move next to the target
				if (x > 0) x = -1;
				else if (x < 0) x = 1;
				if (y > 0) y = -1;
				else if (y < 0) y = 1;

				unit->movepos(src, bl->x + x, bl->y + y, 1, 1);
			}
			clif->fixpos(src);
			clif->spiritball(src);
		}
		hookStop();
	}
	return 0;
}

int hookPost_skill_castend_nodamage_id(int retVal, struct block_list *src, struct block_list *bl, uint16 *skill_id, uint16 *skill_lv, int64 *tick, int *flag) {
	if (bl && *skill_id == SA_DISPELL) {
		struct status_change *tsc = status->get_sc(bl);

		if (tsc && tsc->data[SC_DONTFORGETME])
			status_change_end(bl, SC_DONTFORGETME, INVALID_TIMER);
	}
	return retVal;
}

int hookPost_skill_check_condition_castend(int retVal, struct map_session_data* sd, uint16 *skill_id, uint16 *skill_lv) {
	if (retVal == 1 && *skill_id == MO_EXTREMITYFIST) {
		if (sd && (sd->spiritball < 5 || !sd->sc.data[SC_EXPLOSIONSPIRITS])) {
			hookStop();
			return 0;
		}
	}

	return retVal;
}

int hookPre_status_change_start(struct block_list *src, struct block_list *bl, enum sc_type *type, int *rate, int *val1, int *val2, int *val3, int *val4, int *tick, int *flag) {
	switch (*type) {
	case SC_REFLECTSHIELD:	*flag &= ~(SCFLAG_NOICON);	break;
	case SC_AUTOGUARD:		*flag &= ~(SCFLAG_NOICON);	break;
	case SC_DEFENDER:		*flag &= ~(SCFLAG_NOICON);	break;
	}
	return 0;
}


/*==========================================
* Specifies if item-type should drop unidentified.
*------------------------------------------*/
int itemdb_isidentified(int nameid) {
	int type = itemdb_type(nameid);
	switch (type) {
	case IT_WEAPON:
	case IT_ARMOR:
	case IT_PETARMOR:
		return 1;
	default:
		return 1;
	}
}
/* same as itemdb_isidentified but without a lookup */
int itemdb_isidentified2(struct item_data *data) {
	switch (data->type) {
	case IT_WEAPON:
	case IT_ARMOR:
	case IT_PETARMOR:
		return 1;
	default:
		return 1;
	}
}


HPExport struct hplugin_info pinfo = {
	"HighClass",
	SERVER_TYPE_MAP,
	"1.0",
	HPM_VERSION,
};


HPExport void plugin_init(void) {
	timer = GET_SYMBOL("timer");

	clif = GET_SYMBOL("clif");
	map = GET_SYMBOL("map");
	unit = GET_SYMBOL("unit");
	battle = GET_SYMBOL("battle");
	pc = GET_SYMBOL("pc");
	status = GET_SYMBOL("status");
	skill = GET_SYMBOL("skill");
	itemdb = GET_SYMBOL("itemdb");

	itemdb->isidentified = &itemdb_isidentified;
	itemdb->isidentified2 = &itemdb_isidentified2;

	addHookPre("skill->cell_overlap", hookPre_skill_cell_overlap);
	addHookPre("skill->castend_pos2", hookPre_skill_castend_pos2);
	addHookPre("skill->castend_damage_id", hookPre_skill_castend_damage_id);
	addHookPost("skill->castend_nodamage_id", hookPost_skill_castend_nodamage_id);
	addHookPost("skill->check_condition_castend", hookPost_skill_check_condition_castend);

	addHookPre("status->change_start", hookPre_status_change_start);
}
