#include "fd_sysvar_cache.h"
#include "../fd_acc_mgr.h"
#include "../fd_executor.h"
#include "../fd_system_ids.h"
#include "../context/fd_exec_instr_ctx.h"
#include "../context/fd_exec_txn_ctx.h"

#define FD_SYSVAR_CACHE_MAGIC (0x195a0e78828cacd5UL)

ulong
fd_sysvar_cache_align( void ) {
  return alignof(fd_sysvar_cache_t);
}

ulong
fd_sysvar_cache_footprint( void ) {
  return sizeof(fd_sysvar_cache_t);
}

fd_sysvar_cache_t *
fd_sysvar_cache_new( void * mem ) {

  if( FD_UNLIKELY( !mem ) ) {
    FD_LOG_WARNING(( "NULL mem" ));
    return NULL;
  }

  fd_sysvar_cache_t * cache = (fd_sysvar_cache_t *)mem;
  fd_memset( cache, 0, sizeof(fd_sysvar_cache_t) );

  FD_COMPILER_MFENCE();
  cache->magic = FD_SYSVAR_CACHE_MAGIC;
  FD_COMPILER_MFENCE();
  return cache;
}

void *
fd_sysvar_cache_delete( fd_sysvar_cache_t * cache ) {

  if( FD_UNLIKELY( !cache ) ) {
    FD_LOG_WARNING(( "NULL cache" ));
    return NULL;
  }

  if( FD_UNLIKELY( cache->magic != FD_SYSVAR_CACHE_MAGIC ) ) {
    FD_LOG_WARNING(( "bad magic" ));
    return NULL;
  }

  FD_COMPILER_MFENCE();
  cache->magic = 0UL;
  FD_COMPILER_MFENCE();

  /* Call destroy on all objects.
     This is safe even if these objects logically don't exist
     (destroy is safe on zero-initialized values and is idempotent) */
# define X( type, name ) \
  //type##_destroy( cache->val_##name );
  FD_SYSVAR_CACHE_ITER(X)
# undef X

  return (void *)cache;
}

/* Provide accessor methods */

#define X( type, name )                                                \
  type##_global_t const *                                              \
  fd_sysvar_cache_##name( fd_sysvar_cache_t const * cache ) {          \
    type##_global_t const * val = cache->val_##name;                   \
    return (cache->has_##name) ? val : NULL;                           \
  }
FD_SYSVAR_CACHE_ITER(X)
#undef X

/* Restore sysvars */

# define X( type, name )                                                        \
void                                                                            \
fd_sysvar_cache_restore_##name(                                                 \
  fd_sysvar_cache_t * cache,                                                    \
  fd_funk_t *          funk,                                                    \
  fd_funk_txn_t *     funk_txn,                                                 \
  fd_spad_t *         runtime_spad,                                             \
  fd_wksp_t *         wksp ) {                                                  \
  do {                                                                          \
    fd_pubkey_t const * pubkey = &fd_sysvar_##name##_id;                        \
    FD_TXN_ACCOUNT_DECL( account );                                             \
    int view_err = fd_txn_account_init_from_funk_readonly( account,             \
                                                           pubkey,              \
                                                           funk,                \
                                                           funk_txn );          \
    if( view_err==FD_ACC_MGR_ERR_UNKNOWN_ACCOUNT ) break;                       \
                                                                                \
    if( view_err!=FD_ACC_MGR_SUCCESS ) {                                        \
      char pubkey_cstr[ FD_BASE58_ENCODED_32_SZ ];                              \
      FD_LOG_ERR(( "fd_txn_account_init_from_funk_readonly(%s) failed (%d-%s)", \
                  fd_acct_addr_cstr( pubkey_cstr, pubkey->key ),                \
                  view_err, fd_acc_mgr_strerror( view_err ) ));                 \
    }                                                                           \
                                                                                \
    if( account->const_meta->info.lamports == 0UL ) break;                      \
                                                                                \
    /* Decode new value                                                         \
      type##_decode() does not do heap allocations on failure */                \
    fd_bincode_decode_ctx_t decode = {                                          \
      .data    = account->const_data,                                           \
      .dataend = account->const_data + account->const_meta->dlen,               \
      .wksp    = wksp                                                           \
    };                                                                          \
    ulong total_sz    = 0UL;                                                    \
    int   err         = type##_decode_footprint( &decode, &total_sz );          \
    cache->has_##name = (err==FD_BINCODE_SUCCESS);                              \
    if( FD_UNLIKELY( err ) ) {                                                  \
      FD_LOG_WARNING(( "failed to decode footprint" ));                         \
      break;                                                                    \
    }                                                                           \
                                                                                \
    type##_global_t * mem = fd_spad_alloc( runtime_spad,                        \
                                           type##_align(),                      \
                                           total_sz );                          \
    if( FD_UNLIKELY( !mem ) ) {                                                 \
      FD_LOG_ERR(( "memory allocation failed" ));                               \
    }                                                                           \
    type##_decode_global( mem, &decode );                                       \
    fd_memcpy( cache->val_##name, mem, sizeof(type##_global_t) );               \
  } while(0);                                                                   \
}
  FD_SYSVAR_CACHE_ITER(X)
# undef X

void
fd_sysvar_cache_restore( fd_sysvar_cache_t * cache,
                         fd_funk_t *         funk,
                         fd_funk_txn_t *     funk_txn,
                         fd_spad_t *         runtime_spad,
                         fd_wksp_t *         wksp ) {
# define X( type, name )                                               \
fd_sysvar_cache_restore_##name( cache, funk, funk_txn, runtime_spad, wksp );
  FD_SYSVAR_CACHE_ITER(X)
# undef X
}

# define X( type, name )                                                       \
  type##_global_t const *                                                      \
  fd_sysvar_from_instr_acct_##name( fd_exec_instr_ctx_t const * ctx,           \
                                    ulong                       idx,           \
                                    int *                       err ) {        \
                                                                               \
    if( FD_UNLIKELY( idx >= ctx->instr->acct_cnt ) ) {                         \
      *err = FD_EXECUTOR_INSTR_ERR_NOT_ENOUGH_ACC_KEYS;                        \
      return NULL;                                                             \
    }                                                                          \
                                                                               \
    fd_sysvar_cache_t const * cache = ctx->txn_ctx->sysvar_cache;              \
    type##_global_t const * val = fd_sysvar_cache_##name ( cache );            \
                                                                               \
    ushort idx_in_txn = ctx->instr->accounts[idx].index_in_transaction;        \
    fd_pubkey_t const * addr_have = &ctx->txn_ctx->account_keys[ idx_in_txn ]; \
    fd_pubkey_t const * addr_want = &fd_sysvar_##name##_id;                    \
    if( 0!=memcmp( addr_have, addr_want, sizeof(fd_pubkey_t) ) ) {             \
      *err = FD_EXECUTOR_INSTR_ERR_INVALID_ARG;                                \
      return NULL;                                                             \
    }                                                                          \
                                                                               \
    *err = val ?                                                               \
           FD_EXECUTOR_INSTR_SUCCESS :                                         \
           FD_EXECUTOR_INSTR_ERR_UNSUPPORTED_SYSVAR;                           \
    return val;                                                                \
                                                                               \
  }
  FD_SYSVAR_CACHE_ITER(X)
# undef X

/* https://github.com/anza-xyz/agave/blob/77daab497df191ef485a7ad36ed291c1874596e5/program-runtime/src/sysvar_cache.rs#L223-L234 */
int
fd_check_sysvar_account( fd_exec_instr_ctx_t const * ctx,
                         ulong                       insn_acc_idx,
                         fd_pubkey_t const *         expected_id ) {
  fd_pubkey_t const * txn_accs = ctx->txn_ctx->account_keys;

  if( insn_acc_idx>=ctx->instr->acct_cnt ) {
    return FD_EXECUTOR_INSTR_ERR_NOT_ENOUGH_ACC_KEYS;
  }

  ushort              idx_in_txn   = ctx->instr->accounts[ insn_acc_idx ].index_in_transaction;
  fd_pubkey_t const * insn_acc_key = &txn_accs[ idx_in_txn ];

  if( memcmp( expected_id, insn_acc_key, sizeof(fd_pubkey_t) ) ) {
    return FD_EXECUTOR_INSTR_ERR_INVALID_ARG;
  }
  return FD_EXECUTOR_INSTR_SUCCESS;
}
