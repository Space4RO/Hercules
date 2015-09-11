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
#include "../map/npc.h"
#include "../map/pc.h"
#include "../common/HPMDataCheck.h"


struct ai_data {
	struct block_list *bl;
	int64 last_thinktime;
};


// Min time between AI executions
#define MIN_AI_THINKTIME	1000

static int chrif_disconnectplayer(int retVal, int *fd);

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
		CREATE(ad, struct ai_data, 1);
		ad->bl = &sd->bl;

		idb_put(ai_db, sd->bl.id, &sd->bl);
		addToMSD(sd, ad, HPMi->pid, true);

		if (flag) {
			clif->authfail_fd(sd->fd, 15);
			session[sd->fd]->session_data = NULL;
			sd->fd = 0;
		}
	}
	else {
		idb_remove(ai_db, sd->bl.id);
		removeFromMSD(sd, HPMi->pid);
	}

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
	npc = GET_SYMBOL("npc");
	pc = GET_SYMBOL("pc");

	addScriptCommand("ai", "i", ai);

	addHookPost("chrif->disconnectplayer", chrif_disconnectplayer);
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

int chrif_disconnectplayer(int retVal, int *fd) {
	struct map_session_data *sd = map->id2sd(RFIFOL(*fd, 2));
	struct ai_data *ad;

	if (sd && (ad = getFromSession(sd, HPMi->pid))) {
		map->quit(sd);
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
	) {
		if (ad->bl == NULL)
			return -1;

		mapreg->setreg(script->add_str("$@GID"), ad->bl->id);
		script->run(ev->nd->u.scr.script, ev->pos, ad->bl->id, ev->nd->bl.id);
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