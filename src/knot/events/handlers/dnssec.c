/*  Copyright (C) 2018 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <assert.h>

#include "knot/common/log.h"
#include "knot/conf/conf.h"
#include "knot/dnssec/zone-events.h"
#include "knot/updates/apply.h"
#include "knot/zone/zone.h"
#include "libknot/errcode.h"

static void log_dnssec_next(const knot_dname_t *zone, knot_time_t refresh_at)
{
	char time_str[64] = { 0 };
	struct tm time_gm = { 0 };
	time_t refresh = refresh_at;
	localtime_r(&refresh, &time_gm);
	strftime(time_str, sizeof(time_str), KNOT_LOG_TIME_FORMAT, &time_gm);
	if (refresh_at == 0) {
		log_zone_warning(zone, "DNSSEC, next signing not scheduled");
	} else {
		log_zone_info(zone, "DNSSEC, next signing at %s", time_str);
	}
}

void event_dnssec_reschedule(conf_t *conf, zone_t *zone,
			     const zone_sign_reschedule_t *refresh, bool zone_changed)
{
	time_t now = time(NULL);
	time_t ignore = -1;
	knot_time_t refresh_at = refresh->next_sign;

	if (knot_time_cmp(refresh->next_rollover, refresh_at) < 0) {
		refresh_at = refresh->next_rollover;
	}

	log_dnssec_next(zone->name, (time_t)refresh_at);

	if (refresh->plan_ds_query) {
		zone->timers.next_parent_ds_q = now;
	}

	if (refresh->last_nsec3resalt) {
		zone->timers.last_resalt = refresh->last_nsec3resalt;
	}

	zone_events_schedule_at(zone,
		ZONE_EVENT_DNSSEC, refresh_at ? (time_t)refresh_at : ignore,
		ZONE_EVENT_PARENT_DS_Q, refresh->plan_ds_query ? now : ignore,
		ZONE_EVENT_NSEC3RESALT, refresh->next_nsec3resalt ? refresh->next_nsec3resalt : ignore,
		ZONE_EVENT_NOTIFY, zone_changed ? now : ignore
	);
}

int event_dnssec(conf_t *conf, zone_t *zone)
{
	assert(zone);

	zone_sign_reschedule_t resch = { 0 };
	zone_sign_roll_flags_t r_flags = KEY_ROLL_ALLOW_KSK_ROLL | KEY_ROLL_ALLOW_ZSK_ROLL;
	int sign_flags = 0;

	if (zone->flags & ZONE_FORCE_RESIGN) {
		log_zone_info(zone->name, "DNSSEC, dropping previous "
		              "signatures, re-signing zone");
		zone->flags &= ~ZONE_FORCE_RESIGN;
		sign_flags = ZONE_SIGN_DROP_SIGNATURES;
	} else {
		log_zone_info(zone->name, "DNSSEC, signing zone");
		sign_flags = 0;
	}

	if (zone_events_get_time(zone, ZONE_EVENT_NSEC3RESALT) <= time(NULL)) {
		r_flags |= KEY_ROLL_DO_NSEC3RESALT;
	}

	if (zone->flags & ZONE_FORCE_KSK_ROLL) {
		zone->flags &= ~ZONE_FORCE_KSK_ROLL;
		r_flags |= KEY_ROLL_FORCE_KSK_ROLL;
	}
	if (zone->flags & ZONE_FORCE_ZSK_ROLL) {
		zone->flags &= ~ZONE_FORCE_ZSK_ROLL;
		r_flags |= KEY_ROLL_FORCE_ZSK_ROLL;
	}

	zone_update_t up;
	int ret = zone_update_init(&up, zone, UPDATE_INCREMENTAL);
	if (ret != KNOT_EOK) {
		return ret;
	}

	ret = knot_dnssec_zone_sign(&up, sign_flags, r_flags, &resch);
	if (ret != KNOT_EOK) {
		goto done;
	}

	bool zone_changed = !zone_update_no_change(&up);
	if (zone_changed) {
		ret = zone_update_commit(conf, &up);
		if (ret != KNOT_EOK) {
			goto done;
		}
	}

	// Schedule dependent events
	event_dnssec_reschedule(conf, zone, &resch, zone_changed);

done:
	zone_update_clear(&up);
	return ret;
}
