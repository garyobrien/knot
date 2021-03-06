/*  Copyright (C) 2019 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

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
#include <pthread.h>
#include <sys/types.h>

#include "libdnssec/error.h"
#include "libdnssec/key.h"
#include "libdnssec/keytag.h"
#include "libdnssec/sign.h"
#include "knot/common/log.h"
#include "knot/dnssec/key-events.h"
#include "knot/dnssec/key_records.h"
#include "knot/dnssec/rrset-sign.h"
#include "knot/dnssec/zone-sign.h"
#include "libknot/libknot.h"
#include "contrib/dynarray.h"
#include "contrib/macros.h"
#include "contrib/wire_ctx.h"

typedef struct {
	node_t n;
	uint16_t type;
} type_node_t;

typedef struct {
	knot_dname_t *dname;
	knot_dname_t *hashed_dname;
	list_t *type_list;
} signed_info_t;

/*- private API - common functions -------------------------------------------*/

/*!
 * \brief Initializes RR set and set owner and rclass from template RR set.
 */
static knot_rrset_t rrset_init_from(const knot_rrset_t *src, uint16_t type)
{
	assert(src);
	knot_rrset_t rrset;
	knot_rrset_init(&rrset, src->owner, type, src->rclass, src->ttl);
	return rrset;
}

/*!
 * \brief Create empty RRSIG RR set for a given RR set to be covered.
 */
static knot_rrset_t create_empty_rrsigs_for(const knot_rrset_t *covered)
{
	assert(!knot_rrset_empty(covered));
	return rrset_init_from(covered, KNOT_RRTYPE_RRSIG);
}

static bool apex_rr_changed(const zone_node_t *old_apex,
                            const zone_node_t *new_apex,
                            uint16_t type)
{
	assert(old_apex);
	assert(new_apex);
	knot_rrset_t old_rr = node_rrset(old_apex, type);
	knot_rrset_t new_rr = node_rrset(new_apex, type);

	return !knot_rrset_equal(&old_rr, &new_rr, KNOT_RRSET_COMPARE_WHOLE);
}

static bool apex_dnssec_changed(zone_update_t *update)
{
	if (update->zone->contents == NULL || update->new_cont == NULL) {
		return false;
	}
	return apex_rr_changed(update->zone->contents->apex,
			       update->new_cont->apex, KNOT_RRTYPE_DNSKEY) ||
	       apex_rr_changed(update->zone->contents->apex,
			       update->new_cont->apex, KNOT_RRTYPE_NSEC3PARAM);
}

/*- private API - signing of in-zone nodes -----------------------------------*/

/*!
 * \brief Check if there is a valid signature for a given RR set and key.
 *
 * \param covered  RR set with covered records.
 * \param rrsigs   RR set with RRSIGs.
 * \param key      Signing key.
 * \param ctx      Signing context.
 * \param policy   DNSSEC policy.
 * \param at       RRSIG position.
 *
 * \return The signature exists and is valid.
 */
static bool valid_signature_exists(const knot_rrset_t *covered,
				   const knot_rrset_t *rrsigs,
				   const dnssec_key_t *key,
				   dnssec_sign_ctx_t *ctx,
				   const kdnssec_ctx_t *dnssec_ctx,
				   uint16_t *at)
{
	assert(key);

	if (knot_rrset_empty(rrsigs)) {
		return false;
	}

	uint16_t rrsigs_rdata_count = rrsigs->rrs.count;
	knot_rdata_t *rdata = rrsigs->rrs.rdata;
	for (uint16_t i = 0; i < rrsigs_rdata_count; i++) {
		uint16_t rr_keytag = knot_rrsig_key_tag(rdata);
		uint16_t rr_covered = knot_rrsig_type_covered(rdata);
		rdata = knot_rdataset_next(rdata);

		uint16_t keytag = dnssec_key_get_keytag(key);
		if (rr_keytag != keytag || rr_covered != covered->type) {
			continue;
		}

		if (knot_check_signature(covered, rrsigs, i, key, ctx,
		                         dnssec_ctx) == KNOT_EOK) {
			if (at != NULL) {
				*at = i;
			}
			return true;
		}
	}

	return false;
}

/*!
 * \brief Check if valid signature exists for all keys for a given RR set.
 *
 * \param covered    RR set with covered records.
 * \param rrsigs     RR set with RRSIGs.
 * \param sign_ctx   Local zone signing context.
 *
 * \return Valid signature exists for every key.
 */
static bool all_signatures_exist(const knot_rrset_t *covered,
                                 const knot_rrset_t *rrsigs,
                                 zone_sign_ctx_t *sign_ctx)
{
	assert(sign_ctx);

	for (int i = 0; i < sign_ctx->count; i++) {
		zone_key_t *key = &sign_ctx->keys[i];
		if (!knot_zone_sign_use_key(key, covered)) {
			continue;
		}

		if (!valid_signature_exists(covered, rrsigs, key->key,
		                            sign_ctx->sign_ctxs[i],
		                            sign_ctx->dnssec_ctx, NULL)) {
			return false;
		}
	}

	return true;
}

/*!
 * \brief Note earliest expiration of a signature.
 *
 * \param rrsig       RRSIG rdata.
 * \param expires_at  Current earliest expiration, will be updated.
 */
static void note_earliest_expiration(const knot_rdata_t *rrsig, knot_time_t *expires_at)
{
	assert(rrsig);
	assert(expires_at);

	uint32_t curr_rdata = knot_rrsig_sig_expiration(rrsig);
	knot_time_t current = knot_time_from_u32(curr_rdata);
	*expires_at = knot_time_min(current, *expires_at);
}

/*!
 * \brief Add expired or invalid RRSIGs into the changeset for removal.
 *
 * \param covered     RR set with covered records.
 * \param rrsigs      RR set with RRSIGs.
 * \param sign_ctx    Local zone signing context.
 * \param changeset   Changeset to be updated.
 * \param expires_at  Earliest RRSIG expiration.
 *
 * \return Error code, KNOT_EOK if successful.
 */
static int remove_expired_rrsigs(const knot_rrset_t *covered,
                                 const knot_rrset_t *rrsigs,
                                 zone_sign_ctx_t *sign_ctx,
                                 changeset_t *changeset,
                                 knot_time_t *expires_at)
{
	assert(changeset);

	if (knot_rrset_empty(rrsigs)) {
		return KNOT_EOK;
	}

	assert(rrsigs->type == KNOT_RRTYPE_RRSIG);

	knot_rrset_t to_remove;
	knot_rrset_init_empty(&to_remove);
	int result = KNOT_EOK;

	knot_rrset_t synth_rrsig = rrset_init_from(rrsigs, KNOT_RRTYPE_RRSIG);
	result = knot_synth_rrsig(covered->type, &rrsigs->rrs, &synth_rrsig.rrs, NULL);
	if (result != KNOT_EOK) {
		if (result != KNOT_ENOENT) {
			return result;
		}
		return KNOT_EOK;
	}

	uint16_t rrsig_rdata_count = synth_rrsig.rrs.count;
	for (uint16_t i = 0; i < rrsig_rdata_count; i++) {
		knot_rdata_t *rr = knot_rdataset_at(&synth_rrsig.rrs, i);
		uint16_t keytag = knot_rrsig_key_tag(rr);
		int endloop = 0; // 1 - continue; 2 - break

		for (size_t j = 0; j < sign_ctx->count; j++) {
			zone_key_t *key = &sign_ctx->keys[j];

			if ((!key->is_active && !key->is_post_active) ||
			    dnssec_key_get_keytag(key->key) != keytag) {
				continue;
			}

			result = knot_check_signature(covered, &synth_rrsig, i, key->key,
			                              sign_ctx->sign_ctxs[j], sign_ctx->dnssec_ctx);
			if (result == KNOT_EOK) {
				// valid signature
				note_earliest_expiration(rr, expires_at);
				endloop = 1;
				break;
			} else if (result != DNSSEC_INVALID_SIGNATURE) {
				endloop = 2;
				break;
			}
		}

		if (endloop == 2) {
			break;
		} else if (endloop == 1) {
			continue;
		}

		if (knot_rrset_empty(&to_remove)) {
			to_remove = create_empty_rrsigs_for(&synth_rrsig);
		}

		result = knot_rdataset_add(&to_remove.rrs, rr, NULL);
		if (result != KNOT_EOK) {
			break;
		}
	}

	if (!knot_rrset_empty(&to_remove) && result == KNOT_EOK) {
		result = changeset_add_removal(changeset, &to_remove, 0);
	}

	knot_rdataset_clear(&synth_rrsig.rrs, NULL);
	knot_rdataset_clear(&to_remove.rrs, NULL);

	return result;
}

/*!
 * \brief Add missing RRSIGs into the changeset for adding.
 *
 * \param covered     RR set with covered records.
 * \param rrsigs      RR set with RRSIGs.
 * \param sign_ctx    Local zone signing context.
 * \param changeset   Changeset to be updated.
 * \param expires_at  Earliest RRSIG expiration.
 *
 * \return Error code, KNOT_EOK if successful.
 */
static int add_missing_rrsigs(const knot_rrset_t *covered,
                              const knot_rrset_t *rrsigs,
                              zone_sign_ctx_t *sign_ctx,
                              changeset_t *changeset,
                              knot_time_t *expires_at)
{
	assert(!knot_rrset_empty(covered));
	assert(sign_ctx);
	assert(changeset);

	int result = KNOT_EOK;
	knot_rrset_t to_add;
	knot_rrset_init_empty(&to_add);

	if (covered->type == KNOT_RRTYPE_DNSKEY &&
	    knot_dname_cmp(covered->owner, sign_ctx->dnssec_ctx->zone->dname) == 0 &&
	    sign_ctx->dnssec_ctx->offline_rrsig != NULL) {
		return changeset_add_addition(changeset, sign_ctx->dnssec_ctx->offline_rrsig, CHANGESET_CHECK);
	}

	for (size_t i = 0; i < sign_ctx->count; i++) {
		const zone_key_t *key = &sign_ctx->keys[i];
		if (!knot_zone_sign_use_key(key, covered)) {
			continue;
		}

		if (valid_signature_exists(covered, rrsigs, key->key, sign_ctx->sign_ctxs[i],
		                           sign_ctx->dnssec_ctx, NULL)) {
			continue;
		}

		if (knot_rrset_empty(&to_add)) {
			to_add = create_empty_rrsigs_for(covered);
		}

		result = knot_sign_rrset(&to_add, covered, key->key, sign_ctx->sign_ctxs[i],
		                         sign_ctx->dnssec_ctx, NULL, expires_at);
		if (result != KNOT_EOK) {
			break;
		}
	}

	if (!knot_rrset_empty(&to_add) && result == KNOT_EOK) {
		result = changeset_add_addition(changeset, &to_add, 0);
	}

	knot_rdataset_clear(&to_add.rrs, NULL);

	return result;
}

/*!
 * \brief Add all RRSIGs into the changeset for removal.
 *
 * \param covered    RR set with covered records.
 * \param changeset  Changeset to be updated.
 *
 * \return Error code, KNOT_EOK if successful.
 */
static int remove_rrset_rrsigs(const knot_dname_t *owner, uint16_t type,
                               const knot_rrset_t *rrsigs,
                               changeset_t *changeset)
{
	assert(owner);
	assert(changeset);
	knot_rrset_t synth_rrsig;
	knot_rrset_init(&synth_rrsig, (knot_dname_t *)owner,
	                KNOT_RRTYPE_RRSIG, rrsigs->rclass, rrsigs->ttl);
	int ret = knot_synth_rrsig(type, &rrsigs->rrs, &synth_rrsig.rrs, NULL);
	if (ret != KNOT_EOK) {
		if (ret != KNOT_ENOENT) {
			return ret;
		}
		return KNOT_EOK;
	}

	ret = changeset_add_removal(changeset, &synth_rrsig, 0);
	knot_rdataset_clear(&synth_rrsig.rrs, NULL);

	return ret;
}

/*!
 * \brief Drop all existing and create new RRSIGs for covered records.
 *
 * \param covered    RR set with covered records.
 * \param rrsigs     Existing RRSIGs for covered RR set.
 * \param sign_ctx   Local zone signing context.
 * \param changeset  Changeset to be updated.
 *
 * \return Error code, KNOT_EOK if successful.
 */
static int force_resign_rrset(const knot_rrset_t *covered,
                              const knot_rrset_t *rrsigs,
                              zone_sign_ctx_t *sign_ctx,
                              changeset_t *changeset)
{
	assert(!knot_rrset_empty(covered));

	if (!knot_rrset_empty(rrsigs)) {
		int result = remove_rrset_rrsigs(covered->owner, covered->type,
		                                 rrsigs, changeset);
		if (result != KNOT_EOK) {
			return result;
		}
	}

	return add_missing_rrsigs(covered, NULL, sign_ctx, changeset, NULL);
}

/*!
 * \brief Drop all expired and create new RRSIGs for covered records.
 *
 * \param covered     RR set with covered records.
 * \param rrsigs      Existing RRSIGs for covered RR set.
 * \param sign_ctx    Local zone signing context.
 * \param changeset   Changeset to be updated.
 * \param expires_at  Current earliest expiration, will be updated.
 *
 * \return Error code, KNOT_EOK if successful.
 */
static int resign_rrset(const knot_rrset_t *covered,
                        const knot_rrset_t *rrsigs,
                        zone_sign_ctx_t *sign_ctx,
                        changeset_t *changeset,
                        knot_time_t *expires_at)
{
	assert(!knot_rrset_empty(covered));

	// TODO this function creates some signatures twice (for checking)
	int result = remove_expired_rrsigs(covered, rrsigs, sign_ctx,
	                                   changeset, expires_at);
	if (result != KNOT_EOK) {
		return result;
	}

	return add_missing_rrsigs(covered, rrsigs, sign_ctx, changeset, expires_at);
}

static int remove_standalone_rrsigs(const zone_node_t *node,
                                    const knot_rrset_t *rrsigs,
                                    changeset_t *changeset)
{
	if (rrsigs == NULL) {
		return KNOT_EOK;
	}

	uint16_t rrsigs_rdata_count = rrsigs->rrs.count;
	knot_rdata_t *rdata = rrsigs->rrs.rdata;
	for (uint16_t i = 0; i < rrsigs_rdata_count; ++i) {
		uint16_t type_covered = knot_rrsig_type_covered(rdata);
		if (!node_rrtype_exists(node, type_covered)) {
			knot_rrset_t to_remove;
			knot_rrset_init(&to_remove, rrsigs->owner, rrsigs->type,
			                rrsigs->rclass, rrsigs->ttl);
			int ret = knot_rdataset_add(&to_remove.rrs, rdata, NULL);
			if (ret != KNOT_EOK) {
				return ret;
			}
			ret = changeset_add_removal(changeset, &to_remove, 0);
			knot_rdataset_clear(&to_remove.rrs, NULL);
			if (ret != KNOT_EOK) {
				return ret;
			}
		}
		rdata = knot_rdataset_next(rdata);
	}

	return KNOT_EOK;
}

/*!
 * \brief Update RRSIGs in a given node by updating changeset.
 *
 * \param node        Node to be signed.
 * \param sign_ctx    Local zone signing context.
 * \param changeset   Changeset to be updated.
 * \param expires_at  Current earliest expiration, will be updated.
 *
 * \return Error code, KNOT_EOK if successful.
 */
static int sign_node_rrsets(const zone_node_t *node,
                            zone_sign_ctx_t *sign_ctx,
                            changeset_t *changeset,
                            knot_time_t *expires_at)
{
	assert(node);
	assert(sign_ctx);

	int result = KNOT_EOK;
	knot_rrset_t rrsigs = node_rrset(node, KNOT_RRTYPE_RRSIG);

	for (int i = 0; i < node->rrset_count; i++) {
		knot_rrset_t rrset = node_rrset_at(node, i);
		if (rrset.type == KNOT_RRTYPE_RRSIG) {
			continue;
		}

		if (!knot_zone_sign_rr_should_be_signed(node, &rrset)) {
			continue;
		}

		if (sign_ctx->dnssec_ctx->rrsig_drop_existing) {
			result = force_resign_rrset(&rrset, &rrsigs,
			                            sign_ctx, changeset);
		} else {
			result = resign_rrset(&rrset, &rrsigs, sign_ctx,
			                      changeset, expires_at);
		}

		if (result != KNOT_EOK) {
			return result;
		}
	}

	return remove_standalone_rrsigs(node, &rrsigs, changeset);
}

/*!
 * \brief Struct to carry data for 'sign_data' callback function.
 */
typedef struct {
	zone_tree_t *tree;
	zone_sign_ctx_t *sign_ctx;
	changeset_t changeset;
	knot_time_t expires_at;
	size_t num_threads;
	size_t thread_index;
	size_t rrset_index;
	int errcode;
	int thread_init_errcode;
	pthread_t thread;
} node_sign_args_t;

/*!
 * \brief Sign node (callback function).
 *
 * \param node  Node to be signed.
 * \param data  Callback data, node_sign_args_t.
 */
static int sign_node(zone_node_t **node, void *data)
{
	assert(node && *node);
	assert(data);

	node_sign_args_t *args = (node_sign_args_t *)data;

	if ((*node)->rrset_count == 0) {
		return KNOT_EOK;
	}

	if ((*node)->flags & NODE_FLAGS_NONAUTH) {
		return KNOT_EOK;
	}

	if (args->rrset_index++ % args->num_threads != args->thread_index) {
		return KNOT_EOK;
	}

	int result = sign_node_rrsets(*node, args->sign_ctx,
	                              &args->changeset, &args->expires_at);

	return result;
}

static void *tree_sign_thread(void *_arg)
{
	node_sign_args_t *arg = _arg;
	arg->errcode = zone_tree_apply(arg->tree, sign_node, _arg);
	return NULL;
}

/*!
 * \brief Update RRSIGs in a given zone tree by updating changeset.
 *
 * \param tree        Zone tree to be signed.
 * \param num_threads Number of threads to use for parallel signing.
 * \param zone_keys   Zone keys.
 * \param policy      DNSSEC policy.
 * \param update      Zone update structure to be updated.
 * \param expires_at  Expiration time of the oldest signature in zone.
 *
 * \return Error code, KNOT_EOK if successful.
 */
static int zone_tree_sign(zone_tree_t *tree,
                          size_t num_threads,
                          zone_keyset_t *zone_keys,
                          const kdnssec_ctx_t *dnssec_ctx,
                          zone_update_t *update,
                          knot_time_t *expires_at)
{
	assert(zone_keys);
	assert(dnssec_ctx);
	assert(update);

	int ret = KNOT_EOK;
	node_sign_args_t args[num_threads];
	memset(args, 0, sizeof(args));
	*expires_at = knot_time_add(dnssec_ctx->now, dnssec_ctx->policy->rrsig_lifetime);

	// init context structures
	for (size_t i = 0; i < num_threads; i++) {
		args[i].tree = tree;
		args[i].sign_ctx = zone_sign_ctx(zone_keys, dnssec_ctx);
		if (args[i].sign_ctx == NULL) {
			ret = KNOT_ENOMEM;
			break;
		}
		ret = changeset_init(&args[i].changeset, update->zone->name);
		if (ret != KNOT_EOK) {
			break;
		}
		args[i].expires_at = 0;
		args[i].num_threads = num_threads;
		args[i].thread_index = i;
		args[i].rrset_index = 0;
		args[i].errcode = KNOT_EOK;
		args[i].thread_init_errcode = -1;
	}
	if (ret != KNOT_EOK) {
		for (size_t i = 0; i < num_threads; i++) {
			changeset_clear(&args[i].changeset);
			zone_sign_ctx_free(args[i].sign_ctx);
		}
		return ret;
	}

	if (num_threads == 1) {
		args[0].thread_init_errcode = 0;
		tree_sign_thread(&args[0]);
	} else {
		// start working threads
		for (size_t i = 0; i < num_threads; i++) {
			args[i].thread_init_errcode =
				pthread_create(&args[i].thread, NULL, tree_sign_thread, &args[i]);
		}

		// join those threads that have been really started
		for (size_t i = 0; i < num_threads; i++) {
			if (args[i].thread_init_errcode == 0) {
				args[i].thread_init_errcode = pthread_join(args[i].thread, NULL);
			}
		}
	}

	// collect return code and results
	for (size_t i = 0; i < num_threads && ret == KNOT_EOK; i++) {
		if (args[i].thread_init_errcode != 0) {
			ret = knot_map_errno_code(args[i].thread_init_errcode);
		} else {
			ret = args[i].errcode;
			if (ret == KNOT_EOK) {
				ret = zone_update_apply_changeset(update, &args[i].changeset); // _fix not needed
				*expires_at = knot_time_min(*expires_at, args[i].expires_at);
			}
		}
		changeset_clear(&args[i].changeset);
		zone_sign_ctx_free(args[i].sign_ctx);
	}

	return ret;
}

/*- private API - signing of NSEC(3) in changeset ----------------------------*/

/*!
 * \brief Struct to carry data for changeset signing callback functions.
 */
typedef struct {
	const zone_contents_t *zone;
	changeset_iter_t itt;
	zone_sign_ctx_t *sign_ctx;
	changeset_t changeset;
	knot_time_t expires_at;
	size_t num_threads;
	size_t thread_index;
	size_t rrset_index;
	int errcode;
	int thread_init_errcode;
	pthread_t thread;
} changeset_signing_data_t;

int rrset_add_zone_key(knot_rrset_t *rrset, zone_key_t *zone_key)
{
	if (rrset == NULL || zone_key == NULL) {
		return KNOT_EINVAL;
	}

	dnssec_binary_t dnskey_rdata = { 0 };
	dnssec_key_get_rdata(zone_key->key, &dnskey_rdata);

	return knot_rrset_add_rdata(rrset, dnskey_rdata.data, dnskey_rdata.size, NULL);
}

/*- private API - DNSKEY handling --------------------------------------------*/

static int rrset_add_zone_ds(knot_rrset_t *rrset, zone_key_t *zone_key)
{
	assert(rrset);
	assert(zone_key);

	dnssec_binary_t cds_rdata = { 0 };
	zone_key_calculate_ds(zone_key, &cds_rdata);

	return knot_rrset_add_rdata(rrset, cds_rdata.data, cds_rdata.size, NULL);
}

/*!
 * \brief Wrapper function for changeset signing - to be used with changeset
 *        apply functions.
 *
 * \param chg_rrset  RRSet to be signed (potentially)
 * \param data       Signing data
 *
 * \return Error code, KNOT_EOK if successful.
 */
static int sign_changeset_wrap(knot_rrset_t *chg_rrset,
                               changeset_signing_data_t *args,
                               knot_time_t *expire_at)
{
	// Find RR's node in zone, find out if we need to sign this RR
	const zone_node_t *node =
		zone_contents_find_node(args->zone, chg_rrset->owner);

	// If node is not in zone, all its RRSIGs were dropped - no-op
	if (node) {
		knot_rrset_t zone_rrset = node_rrset(node, chg_rrset->type);
		knot_rrset_t rrsigs = node_rrset(node, KNOT_RRTYPE_RRSIG);

		bool should_sign = knot_zone_sign_rr_should_be_signed(node, &zone_rrset);

		if (should_sign) {
			return resign_rrset(&zone_rrset, &rrsigs, args->sign_ctx,
			                    &args->changeset, expire_at);
		} else {
			/*
			 * If RRSet in zone DOES have RRSIGs although we
			 * should not sign it, DDNS-caused change to node/rr
			 * occurred and we have to drop all RRSIGs.
			 *
			 * OR
			 *
			 * The whole RRSet was removed, but RRSIGs remained in
			 * the zone. We need to drop them as well.
			 */
			return remove_rrset_rrsigs(chg_rrset->owner,
			                           chg_rrset->type, &rrsigs,
			                           &args->changeset);
		}
	}

	return KNOT_EOK;
}

static void *sign_changeset_thread(void *_arg)
{
	changeset_signing_data_t *arg = _arg;

	knot_rrset_t rr = changeset_iter_next(&arg->itt);
	while (!knot_rrset_empty(&rr) && arg->errcode == KNOT_EOK) {
		if (arg->rrset_index++ % arg->num_threads == arg->thread_index) {
			arg->errcode = sign_changeset_wrap(&rr, arg, &arg->expires_at);
		}
		rr = changeset_iter_next(&arg->itt);
	}

	return NULL;
}

/*- public API ---------------------------------------------------------------*/

int knot_zone_sign(zone_update_t *update,
                   zone_keyset_t *zone_keys,
                   const kdnssec_ctx_t *dnssec_ctx,
                   knot_time_t *expire_at)
{
	if (!update || !zone_keys || !dnssec_ctx || !expire_at ||
	    dnssec_ctx->policy->signing_threads < 1) {
		return KNOT_EINVAL;
	}

	int result;

	knot_time_t normal_expire = 0;
	result = zone_tree_sign(update->new_cont->nodes, dnssec_ctx->policy->signing_threads,
	                        zone_keys, dnssec_ctx, update, &normal_expire);
	if (result != KNOT_EOK) {
		return result;
	}

	knot_time_t nsec3_expire = 0;
	result = zone_tree_sign(update->new_cont->nsec3_nodes, dnssec_ctx->policy->signing_threads,
	                        zone_keys, dnssec_ctx, update, &nsec3_expire);
	if (result != KNOT_EOK) {
		return result;
	}

	*expire_at = knot_time_min(normal_expire, nsec3_expire);

	return result;
}

keyptr_dynarray_t knot_zone_sign_get_cdnskeys(const kdnssec_ctx_t *ctx,
					      zone_keyset_t *zone_keys)
{
	keyptr_dynarray_t r = { 0 };
	unsigned crp = ctx->policy->cds_cdnskey_publish;

	if (crp == CDS_CDNSKEY_ROLLOVER || crp == CDS_CDNSKEY_ALWAYS ||
	    crp == CDS_CDNSKEY_DOUBLE_DS) {
		// first, add strictly-ready keys
		for (int i = 0; i < zone_keys->count; i++) {
			zone_key_t *key = &zone_keys->keys[i];
			if (key->is_ready) {
				assert(key->is_ksk);
				keyptr_dynarray_add(&r, &key);
			}
		}

		// second, add active keys
		if ((crp == CDS_CDNSKEY_ALWAYS && r.size == 0) ||
		    (crp == CDS_CDNSKEY_DOUBLE_DS)) {
			for (int i = 0; i < zone_keys->count; i++) {
				zone_key_t *key = &zone_keys->keys[i];
				if (key->is_ksk && key->is_active && !key->is_ready) {
					keyptr_dynarray_add(&r, &key);
				}
			}
		}

		if ((crp != CDS_CDNSKEY_DOUBLE_DS && r.size > 1) ||
		    (r.size > 2)) {
			log_zone_warning(ctx->zone->dname, "DNSSEC, published CDS/CDNSKEY records for too many (%zu) keys", r.size);
		}
	}

	return r;
}

int knot_zone_sign_add_dnskeys(zone_keyset_t *zone_keys, const kdnssec_ctx_t *dnssec_ctx,
			       key_records_t *add_r)
{
	if (add_r == NULL) {
		return KNOT_EINVAL;
	}
	int ret = KNOT_EOK;
	for (int i = 0; i < zone_keys->count; i++) {
		zone_key_t *key = &zone_keys->keys[i];
		if (key->is_public) {
			ret = rrset_add_zone_key(&add_r->dnskey, key);
			if (ret != KNOT_EOK) {
				return ret;
			}
		}
	}
	keyptr_dynarray_t kcdnskeys = knot_zone_sign_get_cdnskeys(dnssec_ctx, zone_keys);
	dynarray_foreach(keyptr, zone_key_t *, ksk_for_cds, kcdnskeys) {
		ret = rrset_add_zone_key(&add_r->cdnskey, *ksk_for_cds);
		if (ret == KNOT_EOK) {
			ret = rrset_add_zone_ds(&add_r->cds, *ksk_for_cds);
		}
	}

	if (dnssec_ctx->policy->cds_cdnskey_publish == CDS_CDNSKEY_EMPTY && ret == KNOT_EOK) {
		const uint8_t cdnskey_empty[5] = { 0, 0, 3, 0, 0 };
		const uint8_t cds_empty[5] = { 0, 0, 0, 0, 0 };
		ret = knot_rrset_add_rdata(&add_r->cdnskey, cdnskey_empty, sizeof(cdnskey_empty), NULL);
		if (ret == KNOT_EOK) {
			ret = knot_rrset_add_rdata(&add_r->cds, cds_empty, sizeof(cds_empty), NULL);
		}
	}

	keyptr_dynarray_free(&kcdnskeys);
	return ret;
}

int knot_zone_sign_update_dnskeys(zone_update_t *update,
                                  zone_keyset_t *zone_keys,
                                  kdnssec_ctx_t *dnssec_ctx,
                                  knot_time_t *next_resign)
{
	if (update == NULL || zone_keys == NULL || dnssec_ctx == NULL) {
		return KNOT_EINVAL;
	}

	const zone_node_t *apex = update->new_cont->apex;
	knot_rrset_t dnskeys = node_rrset(apex, KNOT_RRTYPE_DNSKEY);
	knot_rrset_t cdnskeys = node_rrset(apex, KNOT_RRTYPE_CDNSKEY);
	knot_rrset_t cdss = node_rrset(apex, KNOT_RRTYPE_CDS);
	key_records_t add_r;
	memset(&add_r, 0, sizeof(add_r));
	knot_rrset_t soa = node_rrset(apex, KNOT_RRTYPE_SOA);
	if (knot_rrset_empty(&soa)) {
		return KNOT_EINVAL;
	}

	changeset_t ch;
	int ret = changeset_init(&ch, apex->owner);
	if (ret != KNOT_EOK) {
		return ret;
	}

#define CHECK_RET if (ret != KNOT_EOK) goto cleanup

	// remove all. This will cancel out with additions later
	ret = changeset_add_removal(&ch, &dnskeys, 0);
	CHECK_RET;
	ret = changeset_add_removal(&ch, &cdnskeys, 0);
	CHECK_RET;
	ret = changeset_add_removal(&ch, &cdss, 0);
	CHECK_RET;

	// add DNSKEYs, CDNSKEYs and CDSs
	key_records_init(dnssec_ctx, &add_r);

	if (dnssec_ctx->policy->offline_ksk) {
		ret = kasp_db_load_offline_records(dnssec_ctx->kasp_db, apex->owner, dnssec_ctx->now, next_resign, &add_r);
		if (ret == KNOT_EOK) {
			log_zone_info(dnssec_ctx->zone->dname, "DNSSEC, using offline DNSKEY RRSIG");
		} else {
			log_zone_warning(dnssec_ctx->zone->dname, "DNSSEC, failed to load offline DNSKEY RRSIG (%s)",
					 knot_strerror(ret));
		}
	} else {
		ret = knot_zone_sign_add_dnskeys(zone_keys, dnssec_ctx, &add_r);
	}
	CHECK_RET;

	if (!knot_rrset_empty(&add_r.cdnskey)) {
		ret = changeset_add_addition(&ch, &add_r.cdnskey,
			CHANGESET_CHECK | CHANGESET_CHECK_CANCELOUT);
		CHECK_RET;
	}

	if (!knot_rrset_empty(&add_r.cds)) {
		ret = changeset_add_addition(&ch, &add_r.cds,
			CHANGESET_CHECK | CHANGESET_CHECK_CANCELOUT);
		CHECK_RET;
	}

	if (!knot_rrset_empty(&add_r.dnskey)) {
		ret = changeset_add_addition(&ch, &add_r.dnskey,
			CHANGESET_CHECK | CHANGESET_CHECK_CANCELOUT);
		CHECK_RET;
	}

	if (!knot_rrset_empty(&add_r.rrsig)) {
		dnssec_ctx->offline_rrsig = knot_rrset_copy(&add_r.rrsig, NULL);
	}

	ret = zone_update_apply_changeset(update, &ch);

#undef CHECK_RET

cleanup:
	key_records_clear(&add_r);
	changeset_clear(&ch);
	return ret;
}

bool knot_zone_sign_use_key(const zone_key_t *key, const knot_rrset_t *covered)
{
	if (key == NULL || covered == NULL) {
		return false;
	}

	if (!key->is_active && !key->is_post_active) {
		return false;
	}

	// this may be a problem with offline KSK
	bool cds_sign_by_ksk = true;

	assert(key->is_zsk || key->is_ksk);
	bool is_apex = knot_dname_is_equal(covered->owner,
	                                   dnssec_key_get_dname(key->key));
	if (!is_apex) {
		return key->is_zsk;
	}

	switch (covered->type) {
	case KNOT_RRTYPE_DNSKEY:
		return key->is_ksk;
	case KNOT_RRTYPE_CDS:
	case KNOT_RRTYPE_CDNSKEY:
		return (cds_sign_by_ksk ? key->is_ksk : key->is_zsk);
	default:
		return key->is_zsk;
	}
}

bool knot_zone_sign_soa_expired(const zone_contents_t *zone,
                                const zone_keyset_t *zone_keys,
                                const kdnssec_ctx_t *dnssec_ctx)
{
	if (zone == NULL || zone_keys == NULL || dnssec_ctx == NULL) {
		return false;
	}

	knot_rrset_t soa = node_rrset(zone->apex, KNOT_RRTYPE_SOA);
	assert(!knot_rrset_empty(&soa));
	knot_rrset_t rrsigs = node_rrset(zone->apex, KNOT_RRTYPE_RRSIG);
	zone_sign_ctx_t *sign_ctx = zone_sign_ctx(zone_keys, dnssec_ctx);
	if (sign_ctx == NULL) {
		return false;
	}
	bool exist = all_signatures_exist(&soa, &rrsigs, sign_ctx);
	zone_sign_ctx_free(sign_ctx);
	return !exist;
}

static int sign_changeset(const zone_contents_t *zone,
                          size_t num_threads,
                          zone_update_t *update,
                          zone_keyset_t *zone_keys,
                          const kdnssec_ctx_t *dnssec_ctx,
                          knot_time_t *expire_at)
{
	if (zone == NULL || update == NULL || zone_keys == NULL || dnssec_ctx == NULL) {
		return KNOT_EINVAL;
	}

	int ret = KNOT_EOK;
	changeset_signing_data_t args[num_threads];
	memset(args, 0, sizeof(args));

	// init context structures
	for (size_t i = 0; i < num_threads; i++) {
		args[i].zone = update->new_cont;
		ret = changeset_iter_all(&args[i].itt, &update->change);
		if (ret != KNOT_EOK) {
			break;
		}
		args[i].sign_ctx = zone_sign_ctx(zone_keys, dnssec_ctx);
		if (args[i].sign_ctx == NULL) {
			ret = KNOT_ENOMEM;
			break;
		}
		ret = changeset_init(&args[i].changeset, update->zone->name);
		if (ret != KNOT_EOK) {
			break;
		}
		args[i].expires_at = 0;
		args[i].num_threads = num_threads;
		args[i].thread_index = i;
		args[i].rrset_index = 0;
		args[i].errcode = KNOT_EOK;
		args[i].thread_init_errcode = -1;
	}
	if (ret != KNOT_EOK) {
		for (size_t i = 0; i < num_threads; i++) {
			changeset_iter_clear(&args[i].itt);
			changeset_clear(&args[i].changeset);
			zone_sign_ctx_free(args[i].sign_ctx);
		}
		return ret;
	}

	if (num_threads == 1) {
		args[0].thread_init_errcode = 0;
		sign_changeset_thread(&args[0]);
	} else {
		// start working threads
		for (size_t i = 0; i < num_threads; i++) {
			args[i].thread_init_errcode =
				pthread_create(&args[i].thread, NULL, sign_changeset_thread, &args[i]);
		}

		// join those threads that have been really started
		for (size_t i = 0; i < num_threads; i++) {
			if (args[i].thread_init_errcode == 0) {
				args[i].thread_init_errcode = pthread_join(args[i].thread, NULL);
			}
		}
	}

	if (!knot_rrset_empty(update->change.soa_from)) {
		ret = sign_changeset_wrap(update->change.soa_from, &args[0], expire_at);
	}
	if (ret == KNOT_EOK && !knot_rrset_empty(update->change.soa_to)) {
		ret = sign_changeset_wrap(update->change.soa_to, &args[0], expire_at);
	}

	// collect return code and results
	for (size_t i = 0; i < num_threads && ret == KNOT_EOK; i++) {
		if (args[i].thread_init_errcode != 0) {
			ret = knot_map_errno_code(args[i].thread_init_errcode);
		} else {
			ret = args[i].errcode;
			if (ret == KNOT_EOK) {
				ret = zone_update_apply_changeset_fix(update, &args[i].changeset);
				*expire_at = knot_time_min(*expire_at, args[i].expires_at);
			}
		}
		changeset_iter_clear(&args[i].itt);
		changeset_clear(&args[i].changeset);
		zone_sign_ctx_free(args[i].sign_ctx);
	}

	return ret;
}

int knot_zone_sign_nsecs_in_changeset(const zone_keyset_t *zone_keys,
                                      const kdnssec_ctx_t *dnssec_ctx,
                                      changeset_t *changeset)
{
	if (zone_keys == NULL || dnssec_ctx == NULL || changeset == NULL) {
		return KNOT_EINVAL;
	}

	zone_sign_ctx_t *sign_ctx = zone_sign_ctx(zone_keys, dnssec_ctx);
	if (sign_ctx == NULL) {
		return KNOT_ENOMEM;
	}

	changeset_iter_t itt;
	changeset_iter_add(&itt, changeset);

	knot_rrset_t rr = changeset_iter_next(&itt);
	while (!knot_rrset_empty(&rr)) {
		if (rr.type == KNOT_RRTYPE_NSEC ||
		    rr.type == KNOT_RRTYPE_NSEC3 ||
		    rr.type == KNOT_RRTYPE_NSEC3PARAM) {
			int ret =  add_missing_rrsigs(&rr, NULL, sign_ctx,
			                              changeset, NULL);
			if (ret != KNOT_EOK) {
				changeset_iter_clear(&itt);
				return ret;
			}
		}
		rr = changeset_iter_next(&itt);
	}

	changeset_iter_clear(&itt);
	zone_sign_ctx_free(sign_ctx);

	return KNOT_EOK;
}

bool knot_zone_sign_rr_should_be_signed(const zone_node_t *node,
                                        const knot_rrset_t *rrset)
{
	if (node == NULL || knot_rrset_empty(rrset)) {
		return false;
	}

	// We do not want to sign RRSIGs
	if (rrset->type == KNOT_RRTYPE_RRSIG) {
		return false;
	}

	// At delegation points we only want to sign NSECs and DSs
	if (node->flags & NODE_FLAGS_DELEG) {
		if (!(rrset->type == KNOT_RRTYPE_NSEC ||
		      rrset->type == KNOT_RRTYPE_DS)) {
			return false;
		}
	}

	return true;
}

int knot_zone_sign_update(zone_update_t *update,
                          zone_keyset_t *zone_keys,
                          const kdnssec_ctx_t *dnssec_ctx,
                          knot_time_t *expire_at)
{
	if (update == NULL || zone_keys == NULL || dnssec_ctx == NULL || expire_at == NULL ||
	    dnssec_ctx->policy->signing_threads < 1) {
		return KNOT_EINVAL;
	}

	int ret = KNOT_EOK;

	/* Check if the UPDATE changed DNSKEYs or NSEC3PARAM.
	 * If so, we have to sign the whole zone. */
	const bool full_sign = apex_dnssec_changed(update);
	if (full_sign) {
		ret = knot_zone_sign(update, zone_keys, dnssec_ctx, expire_at);
	} else {
		ret = sign_changeset(update->new_cont, dnssec_ctx->policy->signing_threads,
				     update, zone_keys, dnssec_ctx, expire_at);
	}

	return ret;
}

int knot_zone_sign_soa(zone_update_t *update,
		       const zone_keyset_t *zone_keys,
		       const kdnssec_ctx_t *dnssec_ctx)
{
	knot_rrset_t soa_to = node_rrset(update->new_cont->apex, KNOT_RRTYPE_SOA);
	knot_rrset_t soa_rrsig = node_rrset(update->new_cont->apex, KNOT_RRTYPE_RRSIG);
	changeset_t ch;
	int ret = changeset_init(&ch, update->zone->name);
	if (ret == KNOT_EOK) {
		zone_sign_ctx_t *sign_ctx = zone_sign_ctx(zone_keys, dnssec_ctx);
		if (sign_ctx == NULL) {
			changeset_clear(&ch);
			return KNOT_ENOMEM;
		}
		ret = force_resign_rrset(&soa_to, &soa_rrsig, sign_ctx, &ch);
		if (ret == KNOT_EOK) {
			ret = zone_update_apply_changeset_fix(update, &ch);
		}
		zone_sign_ctx_free(sign_ctx);
	}
	changeset_clear(&ch);
	return ret;
}
