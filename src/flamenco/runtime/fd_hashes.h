#ifndef HEADER_fd_src_flamenco_runtime_fd_hashes_h
#define HEADER_fd_src_flamenco_runtime_fd_hashes_h

#include "../fd_flamenco_base.h"
#include "../types/fd_types.h"
#include "../../funk/fd_funk.h"
#include "../../ballet/lthash/fd_lthash.h"

#define FD_PUBKEY_HASH_PAIR_ALIGN (16UL)
struct __attribute__((aligned(FD_PUBKEY_HASH_PAIR_ALIGN))) fd_pubkey_hash_pair {
  fd_funk_rec_t const * rec;
  fd_hash_t     const * hash;
};
typedef struct fd_pubkey_hash_pair fd_pubkey_hash_pair_t;
#define FD_PUBKEY_HASH_PAIR_FOOTPRINT (sizeof(fd_pubkey_hash_pair_t))

struct fd_accounts_hash_task_info {
  fd_exec_slot_ctx_t *     slot_ctx;
  fd_pubkey_t              acc_pubkey[1];
  fd_hash_t                acc_hash[1];
  uint                     should_erase;
  uint                     hash_changed;
};
typedef struct fd_accounts_hash_task_info fd_accounts_hash_task_info_t;

struct fd_accounts_hash_task_data {
  fd_accounts_hash_task_info_t * info;
  ulong                          info_sz;
  fd_lthash_value_t *            lthash_values;
  ulong                          num_recs;
};
typedef struct fd_accounts_hash_task_data fd_accounts_hash_task_data_t;

union fd_features;
typedef union fd_features fd_features_t;

FD_PROTOTYPES_BEGIN

int
fd_update_hash_bank_exec_hash( fd_exec_slot_ctx_t *           slot_ctx,
                               fd_hash_t *                    hash,
                               fd_capture_ctx_t *             capture_ctx,
                               fd_accounts_hash_task_data_t * task_datas,
                               ulong                          task_datas_cnt,
                               fd_lthash_value_t *            lt_hashes,
                               ulong                          lt_hashes_cnt,
                               ulong                          signature_cnt,
                               fd_spad_t *                    runtime_spad );

void
fd_collect_modified_accounts( fd_exec_slot_ctx_t *           slot_ctx,
                              fd_accounts_hash_task_data_t * task_data,
                              fd_spad_t *                    runtime_spad );

void
fd_account_hash( fd_funk_t *                    funk,
                 fd_funk_txn_t *                funk_txn,
                 fd_accounts_hash_task_info_t * task_info,
                 fd_lthash_value_t *            lt_hash,
                 ulong                          slot,
                 fd_features_t *                features );

int
fd_update_hash_bank_tpool( fd_exec_slot_ctx_t * slot_ctx,
                           fd_capture_ctx_t *   capture_ctx,
                           fd_hash_t *          hash,
                           ulong                signature_cnt,
                           fd_tpool_t *         tpool,
                           fd_spad_t *          runtime_spad );

/* fd_hash_account is the method to compute the account
   hash.  It includes the following content:
    - lamports
    - rent_epoch
    - data
    - executable
    - owner
    - pubkey

   Writes the resulting hash to hash, and returns hash. */

void const *
fd_hash_account( uchar                     hash  [ static 32 ],
                 fd_lthash_value_t       * lthash,
                 fd_account_meta_t const * account,
                 fd_pubkey_t const       * pubkey,
                 uchar const             * data,
                 int                       hash_needed,
                 fd_features_t            *features
 );

/* fd_hash_account_current chooses the correct account hash function
   based on feature activation state. */

#define FD_HASH_JUST_ACCOUNT_HASH   (1)
#define FD_HASH_JUST_LTHASH         (2)
#define FD_HASH_BOTH_HASHES         (3)

void const *
fd_hash_account_current( uchar                      hash  [ static 32 ],
                         fd_lthash_value_t         *lthash,
                         fd_account_meta_t const   *account,
                         fd_pubkey_t       const   *pubkey,
                         uchar const *              data,
                         int                        hash_needed,
                         fd_features_t             *features );

/* Generate a complete accounts_hash of the entire account database. */

int
fd_accounts_hash( fd_funk_t *      funk,
                  fd_slot_bank_t * slot_bank,
                  fd_tpool_t *     tpool,
                  fd_hash_t *      accounts_hash,
                  fd_spad_t *      runtime_spad,
                  int              lthash_enabled,
                  fd_features_t   *features );

/* Generate a non-incremental hash of the entire account database, conditionally including in the epoch account hash. */
int
fd_snapshot_hash( fd_exec_slot_ctx_t * slot_ctx,
                  fd_tpool_t *         tpool,
                  fd_hash_t *          accounts_hash,
                  uint                 check_hash,
                  fd_spad_t *          runtime_spad );

/* Generate an incremental hash of the entire account database, conditionally including in the epoch account hash. */
int
fd_snapshot_inc_hash( fd_exec_slot_ctx_t * slot_ctx,
                      fd_hash_t *          accounts_hash,
                      fd_funk_txn_t *      child_txn,
                      uint                 check_hash,
                      fd_spad_t *          spad );

/* Generate a non-incremental hash of the entire account database, including
   the epoch account hash. It differs from fd_snapshot_hash in that this version
   is used by the snapshot service which doesn't have access to a slot_ctx
   handle. However, it retains a copy of funk, slot_bank, and epoch_bank.
   Do the same for the incremental hash. These functions are also
   responsible for conditionally including the epoch account hash into
   the account hash. These hashes are used by the snapshot service.
   TODO: These should be used to generate the hashes from snapshot loading. */

int
fd_snapshot_service_hash( fd_hash_t *       accounts_hash,
                          fd_hash_t *       snapshot_hash,
                          fd_slot_bank_t *  slot_bank,
                          fd_epoch_bank_t * epoch_bank,
                          fd_funk_t *       funk,
                          fd_tpool_t *      tpool,
                          fd_spad_t *       runtime_spad,
                          fd_features_t    *features );

int
fd_snapshot_service_inc_hash( fd_hash_t *                 accounts_hash,
                              fd_hash_t *                 snapshot_hash,
                              fd_slot_bank_t *            slot_bank,
                              fd_epoch_bank_t *           epoch_bank,
                              fd_funk_t *                 funk,
                              fd_funk_rec_key_t const * * pubkeys,
                              ulong                       pubkeys_len,
                              fd_spad_t *                 spad,
                              fd_features_t              *features  );

void
fd_accounts_check_lthash( fd_funk_t *      funk,
                          fd_funk_txn_t *  funk_txn,
                          fd_slot_bank_t * slot_bank,
                          fd_spad_t *      runtime_spad,
                          fd_features_t  * features );

void
fd_calculate_epoch_accounts_hash_values(fd_exec_slot_ctx_t * slot_ctx);

FD_PROTOTYPES_END

#endif /* HEADER_fd_src_flamenco_runtime_fd_hashes_h */
