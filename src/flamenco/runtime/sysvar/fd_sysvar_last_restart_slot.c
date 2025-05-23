#include "fd_sysvar_last_restart_slot.h"
#include "../../types/fd_types.h"
#include "fd_sysvar.h"
#include "../fd_system_ids.h"
#include "../fd_runtime.h"

void
fd_sysvar_last_restart_slot_init( fd_exec_slot_ctx_t * slot_ctx ) {

  if( !FD_FEATURE_ACTIVE( slot_ctx->slot_bank.slot, slot_ctx->epoch_ctx->features, last_restart_slot_sysvar ) ) {
    FD_LOG_INFO(( "sysvar LastRestartSlot not supported by this ledger version!" ));
    return;
  }

  fd_sol_sysvar_last_restart_slot_t const * sysvar = &slot_ctx->slot_bank.last_restart_slot;

  ulong sz = fd_sol_sysvar_last_restart_slot_size( sysvar );
  uchar enc[ sz ];
  fd_memset( enc, 0, sz );

  fd_bincode_encode_ctx_t encode = {
    .data    = enc,
    .dataend = enc + sz,
  };
  int err = fd_sol_sysvar_last_restart_slot_encode( sysvar, &encode );
  FD_TEST( err==FD_BINCODE_SUCCESS );

  fd_sysvar_set( slot_ctx,
                 fd_sysvar_owner_id.key,
                 &fd_sysvar_last_restart_slot_id,
                 enc, sz,
                 slot_ctx->slot_bank.slot );
}

fd_sol_sysvar_last_restart_slot_t *
fd_sysvar_last_restart_slot_read( fd_sol_sysvar_last_restart_slot_t * result,
                                  fd_sysvar_cache_t const *           sysvar_cache,
                                  fd_funk_t *                         funk,
                                  fd_funk_txn_t *                     funk_txn ) {

  fd_sol_sysvar_last_restart_slot_t const * ret = (fd_sol_sysvar_last_restart_slot_t const *)fd_sysvar_cache_last_restart_slot( sysvar_cache );
  if( FD_UNLIKELY( NULL != ret ) ) {
    fd_memcpy(result, ret, sizeof(fd_sol_sysvar_last_restart_slot_t));
    return result;
  }

  FD_TXN_ACCOUNT_DECL( acc );
  int err = fd_txn_account_init_from_funk_readonly( acc, &fd_sysvar_last_restart_slot_id, funk, funk_txn );
  if( FD_UNLIKELY( err!=FD_ACC_MGR_SUCCESS ) ) return NULL;

  fd_bincode_decode_ctx_t decode = {
    .data    = acc->const_data,
    .dataend = acc->const_data + acc->const_meta->dlen
  };

  ulong total_sz = 0UL;
  err = fd_sol_sysvar_last_restart_slot_decode_footprint( &decode, &total_sz );
  if( FD_UNLIKELY( err!=FD_BINCODE_SUCCESS ) ) {
    return NULL;
  }

  /* This assumes that the result has been setup already with the correct
     memory layout (pointers point to valid sized buffers, etc) */

  fd_sol_sysvar_last_restart_slot_decode( result, &decode );
  return result;
}

/* fd_sysvar_last_restart_slot_update is equivalent to
   Agave's solana_runtime::bank::Bank::update_last_restart_slot */

void
fd_sysvar_last_restart_slot_update( fd_exec_slot_ctx_t * slot_ctx ) {

  /* https://github.com/solana-labs/solana/blob/v1.18.18/runtime/src/bank.rs#L2093-L2095 */
  if( !FD_FEATURE_ACTIVE( slot_ctx->slot_bank.slot, slot_ctx->epoch_ctx->features, last_restart_slot_sysvar ) ) return;

  int   has_current_last_restart_slot = 0;
  ulong     current_last_restart_slot = 0UL;

  /* https://github.com/solana-labs/solana/blob/v1.18.18/runtime/src/bank.rs#L2098-L2106 */
  fd_sol_sysvar_last_restart_slot_t old_account;
  if( fd_sysvar_last_restart_slot_read( &old_account,
                                        slot_ctx->sysvar_cache,
                                        slot_ctx->funk,
                                        slot_ctx->funk_txn ) ) {
    has_current_last_restart_slot = 1;
        current_last_restart_slot = old_account.slot;
  }

  /* https://github.com/solana-labs/solana/blob/v1.18.18/runtime/src/bank.rs#L2108-L2120 */
  /* FIXME: Query hard forks list */
  ulong last_restart_slot = slot_ctx->slot_bank.last_restart_slot.slot;

  /* https://github.com/solana-labs/solana/blob/v1.18.18/runtime/src/bank.rs#L2122-L2130 */
  if( !has_current_last_restart_slot || current_last_restart_slot != last_restart_slot ) {
    fd_sysvar_set(
        slot_ctx, fd_sysvar_owner_id.key,
        &fd_sysvar_last_restart_slot_id,
        &last_restart_slot, sizeof(ulong),
        slot_ctx->slot_bank.slot );
  }
}
