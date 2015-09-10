#include "../common/HPMi.h"
#include "../common/malloc.h"
#include "../common/nullpo.h"
#include "../common/socket.h"
#include "../common/db.h"
#include "../common/utils.h"
#include "../map/clif.h"
#include "../map/script.h"
#include "../map/status.h"
#include "../map/party.h"
#include "../map/npc.h"
#include "../map/pc.h"
#include "../map/mob.h"
#include "../common/HPMDataCheck.h"


static void clif_charnameack(int *fd, struct block_list *bl);
static void clif_charnameupdate(struct map_session_data *ssd);
static int mob_dead(int retVal, struct mob_data *md, struct block_list *src, int *type);
static void status_calc_bl_main(struct block_list *bl, int *flag);

HPExport struct hplugin_info pinfo = {
	"Reputation",
	SERVER_TYPE_MAP,
	"1.2",
	HPM_VERSION,
};


BUILDIN(reputation_update) {
	struct map_session_data *sd = script->rid2sd(st);
	const char *name = script_getstr(st, 2);
	char *reputation;

	if (sd) {
		if (!(reputation = getFromMSD(sd, HPMi->pid))) {
			CREATE(reputation, char, NAME_LENGTH);
			strcpy(reputation, name);
			addToMSD(sd, reputation, HPMi->pid, true);
		}
		else {
			strcpy(reputation, name);
		}

		status_calc_bl(&sd->bl, SCB_ALL);
		clif->charnameack(0, &sd->bl);
	}


	return true;
}


HPExport void plugin_init(void) {
	iMalloc = GET_SYMBOL("iMalloc");
	DB = GET_SYMBOL("DB");
	sockt = GET_SYMBOL("sockt");
	session = GET_SYMBOL("session");
	clif = GET_SYMBOL("clif");
	script = GET_SYMBOL("script");
	status = GET_SYMBOL("status");
	pc = GET_SYMBOL("pc");
	npc = GET_SYMBOL("npc");
	mob = GET_SYMBOL("mob");
	party = GET_SYMBOL("party");

	addHookPre("clif->charnameack", clif_charnameack);
	addHookPre("clif->charnameupdate", clif_charnameupdate);
	addHookPre("status->calc_bl_main", status_calc_bl_main);
	addHookPost("mob->dead", mob_dead);

	addScriptCommand("reputation_update", "s", reputation_update);
}



void clif_charnameupdate(struct map_session_data *ssd) {
	const char *reputation;
	unsigned char buf[103];
	int i;

	if (ssd && (reputation = getFromMSD(ssd, HPMi->pid))) {
		if (strcmp(reputation, "null") != 0) {
			WBUFW(buf, 0) = 0x195;
			WBUFL(buf, 2) = ssd->bl.id;
			memcpy(WBUFP(buf, 6), ssd->status.name, NAME_LENGTH);
			memcpy(WBUFP(buf, 30), reputation, NAME_LENGTH);
		}
		else {
			memcpy(WBUFP(buf, 6), ssd->status.name, NAME_LENGTH);
			WBUFB(buf, 30) = 0;
		}

		if (ssd->status.guild_id && ssd->guild) {
			ARR_FIND(0, ssd->guild->max_member, i, ssd->guild->member[i].account_id == ssd->status.account_id && ssd->guild->member[i].char_id == ssd->status.char_id);
			if (i < ssd->guild->max_member) {
				memcpy(WBUFP(buf, 54), ssd->guild->name, NAME_LENGTH);
				memcpy(WBUFP(buf, 78), ssd->guild->position[i].name, NAME_LENGTH);
			}
		}
		else {
			WBUFB(buf, 54) = 0;
			WBUFB(buf, 78) = 0;
		}

		clif->send(buf, 102, &ssd->bl, AREA);
		hookStop();
	}
}

void clif_charnameack(int *fd, struct block_list *bl) {
	struct map_session_data *ssd;
	unsigned char buf[103];
	const char *reputation;
	int i;

	if (bl && bl->type == BL_PC) {
		ssd = BL_CAST(BL_PC, bl);

		if (ssd && (reputation = getFromMSD(ssd, HPMi->pid))) {
			WBUFW(buf, 0) = 0x195;
			WBUFL(buf, 2) = bl->id;

			if (strcmp(reputation, "null") != 0) {
				memcpy(WBUFP(buf, 6), ssd->status.name, NAME_LENGTH);
				memcpy(WBUFP(buf, 30), reputation, NAME_LENGTH);
			}
			else {
				memcpy(WBUFP(buf, 6), ssd->status.name, NAME_LENGTH);
				WBUFB(buf, 30) = 0;
			}

			if (ssd->status.guild_id && ssd->guild) {
				ARR_FIND(0, ssd->guild->max_member, i, ssd->guild->member[i].account_id == ssd->status.account_id && ssd->guild->member[i].char_id == ssd->status.char_id);
				if (i < ssd->guild->max_member) {
					memcpy(WBUFP(buf, 54), ssd->guild->name, NAME_LENGTH);
					memcpy(WBUFP(buf, 78), ssd->guild->position[i].name, NAME_LENGTH);
				}
			}
			else {
				WBUFB(buf, 54) = 0;
				WBUFB(buf, 78) = 0;
			}

			if (*fd == 0)
				clif->send(buf, 102, bl, AREA);
			else {
				WFIFOHEAD(*fd, 102);
				memcpy(WFIFOP(*fd, 0), buf, 102);
				WFIFOSET(*fd, 102);
			}

			hookStop();
		}
	}
}

int mob_dead(int retVal, struct mob_data *md, struct block_list *src, int *type) {
	if (md && src && src->type == BL_PC) {
		char output[255];
		struct party_data *p;
		struct map_session_data* sd[MAX_PARTY];
		unsigned int i, c, rexp = 0;
		int points = 0, total = 0;

		if (md->spawn && md->spawn->state.boss) {
			sd[0] = (TBL_PC*)src;
			points = pc_readglobalreg(sd[0], script->add_str("MVP_POINTS"));
			total = pc_readglobalreg(sd[0], script->add_str("MVP_POINTS_TOTAL"));
			pc_setglobalreg(sd[0], script->add_str("MVP_POINTS"), points + 1);
			pc_setglobalreg(sd[0], script->add_str("MVP_POINTS_TOTAL"), points + 1);
			sprintf(output, "MVP Gained : 1 points (total %d).", points + 1);
			clif_disp_onlyself(sd[0], output, strlen(output));
		}

		rexp = md->level + rand() % md->level;
		if (md->db->mexp > 0)
			rexp *= rand() % 250;

		if ((p = party->search(((TBL_PC *)src)->status.party_id))) {
			for (i = c = 0; i < MAX_PARTY; i++) {
				if ((sd[c] = p->data[i].sd) == NULL || sd[c]->bl.m != src->m || pc_isdead(sd[c]))
					continue;
				c++;
			}

			if (c > 1) {
				rexp /= c;

				for (i = 0; i < c; i++) {
					pc->calcexp(sd[i], &rexp, &rexp, &md->bl);
					points = pc_readglobalreg(sd[i], script->add_str("PVE_POINTS"));
					pc_setglobalreg(sd[i], script->add_str("PVE_POINTS"), points + rexp);
					sprintf(output, "PVE Gained : %d points (total %d).", rexp, points);
					clif_disp_onlyself(sd[i], output, strlen(output));
				}
			}
		}
		else {
			sd[0] = (TBL_PC*)src;
			pc->calcexp(sd[0], &rexp, &rexp, &md->bl);
			points = pc_readglobalreg(sd[0], script->add_str("PVE_POINTS"));
			pc_setglobalreg(sd[0], script->add_str("PVE_POINTS"), points + rexp);
			sprintf(output, "PVE Gained : %d points (total %d).", rexp, points + rexp);
			clif_disp_onlyself(sd[0], output, strlen(output));
		}
	}

	return retVal;
}

void status_calc_bl_main(struct block_list *bl, int *flag) {
	if (*flag&SCB_BASE && bl && bl->type == BL_PC) {
		struct map_session_data *sd = BL_CAST(BL_PC, bl);
		struct status_data *bst = status->get_base_status(bl);
		struct event_data* ev = (struct event_data*)strdb_get(npc->ev_db, "ReputationSystem::OnCalcBonus");

		if (ev) {
			script->run(ev->nd->u.scr.script, ev->pos, sd->bl.id, ev->nd->bl.id);

			sd->base_status.str += sd->param_bonus[0];
			sd->base_status.agi += sd->param_bonus[1];
			sd->base_status.vit += sd->param_bonus[2];
			sd->base_status.int_ += sd->param_bonus[3];
			sd->base_status.dex += sd->param_bonus[4];
			sd->base_status.luk += sd->param_bonus[5];

			sd->base_status.max_hp = APPLY_RATE(sd->base_status.max_hp, sd->hprate);
			sd->base_status.hp = sd->base_status.max_hp;
			sd->base_status.max_sp = APPLY_RATE(sd->base_status.max_sp, sd->sprate);
			sd->base_status.sp = sd->base_status.sp;
			status_percent_heal(bl, 100, 100);

			sd->max_weight += 2000 * pc->checkskill(sd, ALL_INCCARRY);
		}
	}
}
