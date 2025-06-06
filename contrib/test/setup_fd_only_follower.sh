#!/bin/bash

set -euxo pipefail
IFS=$'\n\t'

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

TMPDIR=$(mktemp --directory --tmpdir="$HOME" tmp-test-tvu-firedancer-dev.XXXXXX)
cd $TMPDIR

cp ../genesis.bin ./

SHRED_VERSION=1234

_PRIMARY_INTERFACE=enp75s0f0
PRIMARY_IP=$(ip addr show $_PRIMARY_INTERFACE | awk '/inet / {print $2}' | cut -d/ -f1 | head -n1)

cleanup() {
  sudo killall -9 -q firedancer-dev || true
  firedancer-dev configure fini all >/dev/null 2>&1 || true
  rm -rf $TMPDIR
}

trap cleanup EXIT SIGINT SIGTERM

FD_DIR="$SCRIPT_DIR/../.."

# RPC_URL="http://$PRIMARY_IP:8899/"

solana-keygen new --no-bip39-passphrase --silent --outfile fd-identity-keypair.json
# solana-keygen new --no-bip39-passphrase --silent --outfile fd-stake-keypair.json
solana-keygen new --no-bip39-passphrase --silent --outfile fd-vote-keypair.json
# solana-keygen new --no-bip39-passphrase --silent --outfile fd-withdrawer-keypair.json

# solana -u $RPC_URL --keypair faucet-keypair.json transfer --allow-unfunded-recipient fd-identity-keypair.json 400000
# solana -u $RPC_URL --keypair fd-identity-keypair.json create-vote-account fd-vote-keypair.json fd-identity-keypair.json fd-withdrawer-keypair.json
# solana -u $RPC_URL --keypair fd-identity-keypair.json create-stake-account fd-stake-keypair.json 300000
# solana -u $RPC_URL --keypair fd-identity-keypair.json delegate-stake fd-stake-keypair.json fd-vote-keypair.json

# solana -u $RPC_URL --keypair fd-identity-keypair.json vote-account fd-vote-keypair.json
# solana -u $RPC_URL --keypair fd-identity-keypair.json stake-account fd-stake-keypair.json

sudo killall -9 -q firedancer-dev || true

# if fd_frank_ledger is not on path then use the one in the home directory
if ! command -v firedancer-dev > /dev/null; then
  PATH="$FD_DIR/build/native/$CC/bin":$PATH
fi

echo "
name = \"fd2test\"
[layout]
    affinity = \"1-37\"
    bank_tile_count = 1
    verify_tile_count = 16
    shred_tile_count = 1
[gossip]
    entrypoints = [\"$1:8700\"]
    port = 8800
[tiles]
    [tiles.repair]
        repair_intake_listen_port = 8801
        repair_serve_listen_port = 8802
    [tiles.replay]
        capture = \"firedancer_dev.solcap\"
        blockstore_publish = true
        genesis = \"genesis.bin\"
        tpool_thread_count = 8
        funk_sz_gb = 32
        funk_rec_max = 10000000
        funk_txn_max = 1024
        cluster_version = 2004
    [tiles.net]
        interface = \"$_PRIMARY_INTERFACE\"
[log]
    path = \"firedancer_dev_follower.log\"
    level_stderr = \"INFO\"
    level_flush = \"ERR\"
[development]
    topology = \"firedancer\"
[consensus]
    vote = false
    identity_path = \"fd-identity-keypair.json\"
    vote_account_path = \"fd-vote-keypair.json\"
    expected_shred_version = $SHRED_VERSION
" > firedancer-dev.toml

sudo $FD_DIR/build/native/$CC/bin/firedancer-dev configure init kill --config $(readlink -f firedancer-dev.toml)
sudo $FD_DIR/build/native/$CC/bin/firedancer-dev configure init hugetlbfs --config $(readlink -f firedancer-dev.toml)
sudo $FD_DIR/build/native/$CC/bin/firedancer-dev configure init ethtool-channels --config $(readlink -f firedancer-dev.toml)
sudo $FD_DIR/build/native/$CC/bin/firedancer-dev configure init ethtool-gro ethtool-loopback --config $(readlink -f firedancer-dev.toml)
sudo $FD_DIR/build/native/$CC/bin/firedancer-dev configure init keys --config $(readlink -f firedancer-dev.toml)

sudo gdb -iex="set debuginfod enabled on" -ex=r --args $FD_DIR/build/native/$CC/bin/firedancer-dev dev --no-configure --log-path $(readlink -f firedancer-dev.log) --config $(readlink -f firedancer-dev.toml)
