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

#pragma once

#include "knot/conf/conf.h"
#include "knot/zone/zone.h"
#include "knot/zone/zonedb.h"

/*!
 * \brief Opens zone timers db. No-op without LMDB support.
 *
 * \param conf  Server-wide config.
 *
 * \return KNOT_E*
 */
int open_timers_db(conf_t *conf);

/*!
 * \brief Closes zone timers db.
 *
 * \param conf  Server-wide config.
 */
void close_timers_db(conf_t *conf);

/*!
 * \brief Reads zone timers from timers db.
 *        Currently these events are read (and stored):
 *          ZONE_EVENT_REFRESH
 *          ZONE_EVENT_EXPIRE
 *          ZONE_EVENT_FLUSH
 *
 * \param conf     Server-wide config.
 * \param zone     Zone to read timers for.
 * \param timers   Output array with timers (size must be ZONE_EVENT_COUNT).
 *
 * \return KNOT_E*
 */
int read_zone_timers(conf_t *conf, const zone_t *zone, time_t *timers);

/*!
 * \brief Writes zone timers to timers db.
 *
 * \param conf     Server-wide config.
 * \param zone     Zone to store timers for.
 *
 * \return KNOT_E*
 */
int write_zone_timers(conf_t *conf, zone_t *zone);

/*!
 * \brief Removes stale zones info from timers db.
 *
 * \param conf     Server-wide config.
 * \param zone_db  Current zone database.
 * \return KNOT_EOK or an error
 */
int sweep_timer_db(conf_t *conf, knot_zonedb_t *zone_db);

