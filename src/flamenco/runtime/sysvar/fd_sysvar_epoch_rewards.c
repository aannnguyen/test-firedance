#include "fd_sysvar_epoch_rewards.h"
#include "fd_sysvar.h"
#include "../fd_acc_mgr.h"
#include "../fd_runtime.h"
#include "../fd_borrowed_account.h"
#include "../fd_system_ids.h"
#include "../context/fd_exec_epoch_ctx.h"

static void
write_epoch_rewards( fd_exec_slot_ctx_t * slot_ctx, fd_sysvar_epoch_rewards_t * epoch_rewards ) {
  ulong sz = fd_sysvar_epoch_rewards_size( epoch_rewards );
  uchar enc[sz];
  fd_memset( enc, 0, sz );
  fd_bincode_encode_ctx_t ctx;
  ctx.data = enc;
  ctx.dataend = enc + sz;
  if ( fd_sysvar_epoch_rewards_encode( epoch_rewards, &ctx ) ) {
    FD_LOG_ERR(("fd_sysvar_epoch_rewards_encode failed"));
  }

  fd_sysvar_set( slot_ctx, fd_sysvar_owner_id.key, &fd_sysvar_epoch_rewards_id, enc, sz, slot_ctx->slot_bank.slot );
}

fd_sysvar_epoch_rewards_t *
fd_sysvar_epoch_rewards_read( fd_sysvar_epoch_rewards_t * result,
                              fd_sysvar_cache_t const *   sysvar_cache,
                              fd_funk_t *                 funk,
                              fd_funk_txn_t *             funk_txn ) {
  fd_sysvar_epoch_rewards_t const * ret = (fd_sysvar_epoch_rewards_t const *)fd_sysvar_cache_epoch_rewards( sysvar_cache );
  if( FD_UNLIKELY( NULL != ret ) ) {
    fd_memcpy(result, ret, sizeof(fd_sysvar_epoch_rewards_t));
    return result;
  }

  FD_TXN_ACCOUNT_DECL( acc );
  int err = fd_txn_account_init_from_funk_readonly( acc, &fd_sysvar_epoch_rewards_id, funk, funk_txn );
  if( FD_UNLIKELY( err != FD_ACC_MGR_SUCCESS ) ) {
    return NULL;
  }

  fd_bincode_decode_ctx_t decode = {
    .data    = acc->const_data,
    .dataend = acc->const_data + acc->const_meta->dlen
  };

  ulong total_sz = 0UL;
  err = fd_sysvar_epoch_rewards_decode_footprint( &decode, &total_sz );

  if( FD_UNLIKELY( err ) ) {
    return NULL;
  }

  /* We assume here that the data structure is properly allocated already.
     This could potentially be unsafe if not handled correctly by the caller. */
  fd_sysvar_epoch_rewards_decode( result, &decode );

  return result;
}

/* Since there are multiple sysvar epoch rewards updates within a single slot,
   we need to ensure that the cache stays updated after each change (versus with other
   sysvars which only get updated once per slot and then synced up after) */
void
fd_sysvar_epoch_rewards_distribute( fd_exec_slot_ctx_t * slot_ctx,
                                    ulong                distributed,
                                    fd_spad_t *          runtime_spad ) {
    fd_sysvar_epoch_rewards_t epoch_rewards[1];
    if ( FD_UNLIKELY( fd_sysvar_epoch_rewards_read( epoch_rewards,
                                                    slot_ctx->sysvar_cache,
                                                    slot_ctx->funk,
                                                    slot_ctx->funk_txn ) == NULL ) ) {
      FD_LOG_ERR(( "failed to read sysvar epoch rewards" ));
    }
    FD_TEST( epoch_rewards->active );

    FD_TEST( fd_ulong_sat_add( epoch_rewards->distributed_rewards, distributed ) <= epoch_rewards->total_rewards );

    epoch_rewards->distributed_rewards += distributed;

    write_epoch_rewards( slot_ctx, epoch_rewards );

    /* Sync the epoch rewards sysvar cache entry with the account */
    fd_sysvar_cache_restore_epoch_rewards( slot_ctx->sysvar_cache,
                                           slot_ctx->funk,
                                           slot_ctx->funk_txn,
                                           runtime_spad,
                                           slot_ctx->runtime_wksp );
}

void
fd_sysvar_epoch_rewards_set_inactive( fd_exec_slot_ctx_t * slot_ctx,
                                      fd_spad_t *          runtime_spad ) {
    fd_sysvar_epoch_rewards_t epoch_rewards[1];
    if ( FD_UNLIKELY( fd_sysvar_epoch_rewards_read( epoch_rewards,
                                                    slot_ctx->sysvar_cache,
                                                    slot_ctx->funk,
                                                    slot_ctx->funk_txn ) == NULL ) ) {
      FD_LOG_ERR(( "failed to read sysvar epoch rewards" ));
    }

    if ( FD_LIKELY( FD_FEATURE_ACTIVE( slot_ctx->slot_bank.slot, slot_ctx->epoch_ctx->features, partitioned_epoch_rewards_superfeature ) ) ) {
      FD_TEST( epoch_rewards->total_rewards >= epoch_rewards->distributed_rewards );
    } else {
      FD_TEST( epoch_rewards->total_rewards == epoch_rewards->distributed_rewards );
    }


    epoch_rewards->active = 0;

    write_epoch_rewards( slot_ctx, epoch_rewards );

    /* Sync the epoch rewards sysvar cache entry with the account */
    fd_sysvar_cache_restore_epoch_rewards( slot_ctx->sysvar_cache,
                                           slot_ctx->funk,
                                           slot_ctx->funk_txn,
                                           runtime_spad,
                                           slot_ctx->runtime_wksp );
}

/* Create EpochRewards syavar with calculated rewards

   https://github.com/anza-xyz/agave/blob/cbc8320d35358da14d79ebcada4dfb6756ffac79/runtime/src/bank/partitioned_epoch_rewards/sysvar.rs#L25 */
void
fd_sysvar_epoch_rewards_init(
    fd_exec_slot_ctx_t * slot_ctx,
    ulong total_rewards,
    ulong distributed_rewards,
    ulong distribution_starting_block_height,
    ulong num_partitions,
    fd_point_value_t point_value,
    const fd_hash_t * last_blockhash
) {
    FD_TEST( total_rewards >= distributed_rewards );

    fd_sysvar_epoch_rewards_t epoch_rewards = {
      .distribution_starting_block_height = distribution_starting_block_height,
      .num_partitions = num_partitions,
      .total_points = point_value.points,
      .total_rewards = total_rewards,
      .distributed_rewards = distributed_rewards,
      .active = 1
    };

    /* On clusters where partitioned_epoch_rewards_superfeature is enabled, we should use point_value.rewards.
       On other clusters, including those where enable_partitioned_epoch_reward is enabled, we should use total_rewards.

       https://github.com/anza-xyz/agave/blob/b9c9ecccbb05d9da774d600bdbef2cf210c57fa8/runtime/src/bank/partitioned_epoch_rewards/sysvar.rs#L36-L43 */
    if( FD_LIKELY( FD_FEATURE_ACTIVE( slot_ctx->slot_bank.slot, slot_ctx->epoch_ctx->features, partitioned_epoch_rewards_superfeature ) ) ) {
      epoch_rewards.total_rewards = point_value.rewards;
    }

    fd_memcpy( &epoch_rewards.parent_blockhash, last_blockhash, FD_HASH_FOOTPRINT );

    write_epoch_rewards( slot_ctx, &epoch_rewards );
}
