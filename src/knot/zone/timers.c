/*  Copyright (C) 2014 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "libknot/dname.h"
#include "common/namedb/namedb.h"
#include "common/namedb/namedb_lmdb.h"
#include "knot/zone/timers.h"
#include "knot/zone/zonedb.h"

/* ---- Knot-internal event code to db key defines and lookup table --------- */

#define KEY_UNKNOWN 0
#define KEY_REFRESH 1
#define KEY_EXPIRE 2
#define KEY_FLUSH 3

static const uint8_t event_id_to_key[ZONE_EVENT_COUNT] = {
 [ZONE_EVENT_REFRESH] = KEY_REFRESH,
 [ZONE_EVENT_EXPIRE] = KEY_EXPIRE,
 [ZONE_EVENT_FLUSH] = KEY_FLUSH
};

static bool event_persistent(size_t event)
{
	return event_id_to_key[event] != KEY_UNKNOWN;
}

#undef KEY_UNKNOWN
#undef KEY_REFRESH
#undef KEY_EXPIRE
#undef KEY_FLUSH

/* ----- Key and value init macros ------------------------------------------ */

// Key is a zone name + event code.
#define KEY_INIT(zone, event_id, buf) { .len = sizeof(buf), .data = buf }; \
	memcpy(buf, zone->name, sizeof(buf) - 1); \
	buf[sizeof(buf) - 1] = event_id_to_key[event_id]

// Value is a 64bit timer.
#define VAL_INIT(buf, timer) { .len = sizeof(buf), .data = buf }; \
	knot_wire_write_u64(buf, timer)

/*! \brief Stores timers for persistent events. */
static int store_timers(knot_txn_t *txn, zone_t *zone)
{
	for (size_t event = 0; event < ZONE_EVENT_COUNT; ++event) {
		if (!event_persistent(event)) {
			continue;
		}

		// Create key
		uint8_t buf[knot_dname_size(zone->name) + 1];
		knot_val_t key = KEY_INIT(zone, event, buf);

		// Create value
		uint8_t packed_timer[sizeof(int64_t)];
		knot_val_t val = VAL_INIT(packed_timer,
		                          (int64_t)zone_events_get_time(zone, event));

		// Store
		int ret = namedb_lmdb_api()->insert(txn, &key, &val, 0);
		if (ret != KNOT_EOK) {
			return ret;
		}
		

	}

	return KNOT_EOK;
}

/*! \brief Reads timers for persistent events. */
static int read_timers(knot_txn_t *txn, const zone_t *zone, time_t *timers)
{
	const struct namedb_api *db_api = namedb_lmdb_api();

	for (size_t event = 0; event < ZONE_EVENT_COUNT; ++event) {
		if (!event_persistent(event)) {
			timers[event] = 0;
			continue;
		}

		const size_t dsize = knot_dname_size(zone->name);
		uint8_t buf[dsize + 1];
		knot_val_t key = KEY_INIT(zone, event, buf);

		knot_val_t val;
		int ret = db_api->find(txn, &key, &val, 0);
		if (ret != KNOT_EOK) {
			if (ret == KNOT_ENOENT) {
				// New zone or new event type.
				timers[event] = 0;
				continue;
			} else {
				return ret;
			}
		}
		
		timers[event] = (time_t)knot_wire_read_u64(val.data);
	}

	return KNOT_EOK;
}

/* -------- API ------------------------------------------------------------- */

int open_timers_db(conf_t *conf)
{
#ifndef HAVE_LMDB
	// No-op if we don't have lmdb, all other operations will no-op as well then
	return KNOT_EOK;
#else
	conf->timers_db = namedb_lmdb_api()->init(conf->storage, NULL);
	if (conf->timers_db == NULL) {
		return KNOT_ERROR;
	}

	return KNOT_EOK;
#endif
}

void close_timers_db(conf_t *conf)
{
	if (conf->timers_db) {
		namedb_lmdb_api()->deinit(conf->timers_db);
		conf->timers_db = NULL;
	}
}

int read_zone_timers(conf_t *conf, const zone_t *zone, time_t *timers)
{
	if (conf->timers_db == NULL) {
		memset(timers, 0, ZONE_EVENT_COUNT * sizeof(time_t));
		return KNOT_EOK;
	}

	const struct namedb_api *db_api = namedb_lmdb_api();

	knot_txn_t txn;
	int ret = db_api->txn_begin(conf->timers_db, &txn, NAMEDB_RDONLY);
	if (ret != KNOT_EOK) {
		return ret;
	}

	ret = read_timers(&txn, zone, timers);
	if (ret != KNOT_EOK) {
		db_api->txn_abort(&txn);
		return ret;
	}

	ret = db_api->txn_commit(&txn);
	if (ret != KNOT_EOK) {
		return ret;
	}

	return KNOT_EOK;
}

int write_zone_timers(conf_t *conf, zone_t *zone)
{
	if (conf->timers_db == NULL) {
		return KNOT_EOK;
	}
	

	const struct namedb_api *db_api = namedb_lmdb_api();

	knot_txn_t txn;
	int ret = db_api->txn_begin(conf->timers_db, &txn, 0);
	if (ret != KNOT_EOK) {
		return ret;
	}

	ret = store_timers(&txn, zone);
	if (ret != KNOT_EOK) {
		db_api->txn_abort(&txn);
		return ret;
	}

	return db_api->txn_commit(&txn);
}

int sweep_timer_db(conf_t *conf, knot_zonedb_t *zone_db)
{
	if (conf->timers_db == NULL) {
		return KNOT_EOK;
	}
	const struct namedb_api *db_api = namedb_lmdb_api();

	knot_txn_t txn;
	int ret = db_api->txn_begin(conf->timers_db, &txn, NAMEDB_SORTED);
	if (ret != KNOT_EOK) {
		return ret;
	}

	if (db_api->count(&txn) == 0) {
		db_api->txn_abort(&txn);
		return KNOT_EOK;
	}

	knot_iter_t *it = db_api->iter_begin(&txn, 0);
	if (it == NULL) {
		db_api->txn_abort(&txn);
		return KNOT_ERROR;
	}

	while (it) {
		knot_val_t key;
		ret = db_api->iter_key(it, &key);
		if (ret != KNOT_EOK) {
			db_api->txn_abort(&txn);
			return ret;
		}
		if (!knot_zonedb_find(zone_db, (knot_dname_t *)key.data)) {
			// Delete obsolete timers
			db_api->del(&txn, &key);
		}
		it = db_api->iter_next(it);
	}
	db_api->iter_finish(it);

	return db_api->txn_commit(&txn);
}


