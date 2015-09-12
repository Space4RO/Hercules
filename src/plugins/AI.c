#include "../common/HPMi.h"
#include "../common/nullpo.h"
#include "../common/malloc.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/db.h"
#include "../map/map.h"
#include "../map/mapreg.h"
#include "../map/clif.h"
#include "../map/chrif.h"
#include "../map/script.h"
#include "../map/unit.h"
#include "../map/npc.h"
#include "../map/pc.h"
#include "../common/HPMDataCheck.h"


struct ai_data {
	struct block_list *bl;
	struct script_state *st;

	int64 last_thinktime;
};


// Min time between AI executions
#define MIN_AI_THINKTIME	1000

static int chrif_disconnectplayer(int *fd);
static void clif_quitsave(int *fd, struct map_session_data *sd);
static int pc_setpos(int retVal, struct map_session_data *sd, unsigned short *map_index, int *x, int *y, clr_type *clrtype);

static int ai_timer(int tid, int64 tick, int id, intptr_t data);
static int ai_timer_sub(struct block_list *bl, va_list ap);
static void ai_foreach(int(*func)(struct block_list *, va_list), ...);

static DBMap *ai_db = NULL;



HPExport struct hplugin_info pinfo = {
	"AI",
	SERVER_TYPE_MAP,
	"1.0",
	HPM_VERSION,
};

BUILDIN(ai) {
	struct map_session_data *sd;
	struct ai_data *ad;
	int flag = script_getnum(st, 2);

	if ((sd = script->rid2sd(st))
	&& !(ad = getFromMSD(sd, HPMi->pid))
	) {
		if (flag) {
			clif->authfail_fd(sd->fd, 15);
		}

		CREATE(ad, struct ai_data, 1);
		ad->bl = &sd->bl;
		addToMSD(sd, ad, HPMi->pid, true);
		idb_put(ai_db, sd->bl.id, &sd->bl);
	}
	else {
		idb_remove(ai_db, sd->bl.id);
		removeFromMSD(sd, HPMi->pid);
	}

	return true;
}

int buildin_getareamonsters_sub(struct block_list *bl, va_list ap) {
	int *count = va_arg(ap, int *);
	mapreg->setreg(reference_uid(script->add_str("$@ARGS"), *count), bl->id);
	*count += 1;
	return 0;
}

BUILDIN(getareamonsters) {
	struct map_session_data *sd = script->rid2sd(st);
	int range = script_getnum(st, 2);
	int x0, y0, x1, y1;
	int count = 0;

	if (sd) {
		x0 = sd->bl.x - range;
		y0 = sd->bl.y - range;
		x1 = sd->bl.x + range;
		y1 = sd->bl.y + range;

		map->foreachinarea(buildin_getareamonsters_sub, sd->bl.m, x0, y0, x1, y1, BL_MOB, &count);
	}

	script_pushint(st, count);
	return true;
}

HPExport void plugin_init(void) {
	nullpo = GET_SYMBOL("nullpo");
	iMalloc = GET_SYMBOL("iMalloc");
	sockt = GET_SYMBOL("sockt");
	timer = GET_SYMBOL("timer");
	DB = GET_SYMBOL("DB");
	session = GET_SYMBOL("session");
	map = GET_SYMBOL("map");
	mapreg = GET_SYMBOL("mapreg");
	clif = GET_SYMBOL("clif");
	chrif = GET_SYMBOL("chrif");
	script = GET_SYMBOL("script");
	unit = GET_SYMBOL("unit");
	npc = GET_SYMBOL("npc");
	pc = GET_SYMBOL("pc");

	addScriptCommand("ai", "i", ai);
	addScriptCommand("getareamonsters", "i", getareamonsters);

	addHookPre("clif->quitsave", clif_quitsave);
	addHookPre("chrif->disconnectplayer", chrif_disconnectplayer);
	addHookPost("pc->setpos", pc_setpos);
}


HPExport void server_preinit(void) {
	
}

HPExport void server_online(void) {
	ai_db = idb_alloc(DB_OPT_BASE);

	timer->add_interval(timer->gettick() + MIN_AI_THINKTIME, ai_timer, 0, 0, MIN_AI_THINKTIME);
}

HPExport void server_post_final(void) {
	db_destroy(ai_db);
}

int chrif_disconnectplayer(int *fd) {
	struct map_session_data *sd;
	struct ai_data *ad;
	int account_id = RFIFOL(*fd, 2);

	sd = map->id2sd(account_id);

	if (sd && (ad = getFromMSD(sd, HPMi->pid))) {
		removeFromMSD(sd, HPMi->pid);
		idb_remove(ai_db, sd->bl.id);
		map->quit(sd);
	}

	return 0;
}

void clif_quitsave(int *fd, struct map_session_data *sd) {
	struct ai_data *ad;

	if (sd && (ad = getFromMSD(sd, HPMi->pid))) {
		session[*fd]->session_data = NULL;
		sd->fd = 0;
		hookStop();
	}
}

int pc_setpos(int retVal, struct map_session_data *sd, unsigned short *map_index, int *x, int *y, clr_type *clrtype) {
	struct ai_data *ad;

	if (sd && (ad = getFromMSD(sd, HPMi->pid))) {
		clif->pLoadEndAck(0, sd);
	}

	return retVal;
}


int ai_timer_sub(struct block_list *bl, va_list ap) {
	struct map_session_data *sd;
	struct event_data *ev;
	struct ai_data *ad;
	int64 tick;

	nullpo_retr(-1, bl);
	tick = va_arg(ap, int64);

	sd = BL_CAST(BL_PC, bl);
	ev = strdb_get(npc->ev_db, "AI::OnThinkedEvent");

	if (sd && ev
	&& (ad = getFromMSD(sd, HPMi->pid))
	&& (DIFF_TICK(ad->last_thinktime, tick) < MIN_AI_THINKTIME)
	&& (DIFF_TICK(sd->ud.attackabletime, tick) < 0)
	) {
		mapreg->setreg(script->add_str("$@GID"), sd->bl.id);
		script->run(ev->nd->u.scr.script, ev->pos, sd->bl.id, ev->nd->bl.id);
	}

	return 1;
}

/*==========================================
*
*------------------------------------------*/
int ai_timer(int tid, int64 tick, int id, intptr_t data) {
	ai_foreach(ai_timer_sub, tick);
	return 0;
}

/*==========================================
*
*------------------------------------------*/
void ai_foreach(int(*func)(struct block_list *, va_list), ...) {
	DBIterator *iter;
	struct block_list *bl;

	iter = db_iterator(ai_db);
	for (bl = dbi_first(iter); dbi_exists(iter); bl = dbi_next(iter)) {
		int ret;
		va_list args;

		va_start(args, func);
		ret = func(bl, args);
		va_end(args);
	}
	dbi_destroy(iter);
}