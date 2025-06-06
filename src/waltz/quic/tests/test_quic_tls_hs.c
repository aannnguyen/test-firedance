/* test_quic_tls_hs performs a handshake using the fd_quic_tls API.
   (fd_quic_tls is a light wrapper over fd_tls) */

#include "../../tls/test_tls_helper.h"
#include "../tls/fd_quic_tls.h"
#include "../templ/fd_quic_transport_params.h"

// test transport parameters
static uchar const test_tp[] =
  "\x01\x04\x80\x00\xea\x60\x04\x04\x80\x10\x00\x00"
  "\x05\x04\x80\x10\x00\x00\x06\x04\x80\x10\x00\x00\x07\x04\x80\x10"
  "\x00\x00\x08\x02\x40\x80\x09\x02\x40\x80\x0a\x01\x03\x0b\x01\x19"
  "\x0e\x01\x08\x0f\x08\xec\x73\x1b\x41\xa0\xd5\xc6\xfe";


typedef struct my_quic_tls my_quic_tls_t;
struct my_quic_tls {
  int is_server;
  int is_hs_complete;

  int state;
  int sec_level;
};

static void
my_hs_complete( fd_quic_tls_hs_t *           hs,
                void *                       context ) {
  (void)hs;

  FD_LOG_DEBUG(( "callback handshake complete" ));

  my_quic_tls_t * ctx = (my_quic_tls_t*)context;
  ctx->is_hs_complete = 1;
}

static void
my_secrets( fd_quic_tls_hs_t *           hs,
            void *                       context,
            fd_quic_tls_secret_t const * secret ) {
  (void)context;
  FD_TEST( secret );
  FD_LOG_INFO(( "callback secrets (%s, level=%u)",
                hs->is_server ? "server" : "client", secret->enc_level ));
  FD_LOG_HEXDUMP_INFO(( "read secret",  secret->read_secret,  FD_QUIC_SECRET_SZ ));
  FD_LOG_HEXDUMP_INFO(( "write secret", secret->write_secret, FD_QUIC_SECRET_SZ ));
}

void
my_transport_params( void *        context,
                     uchar const * quic_tp,
                     ulong         quic_tp_sz ) {
  (void)context;
  FD_TEST( quic_tp_sz == sizeof(test_tp)-1 );
  FD_TEST( 0==memcmp( quic_tp, test_tp, quic_tp_sz ) );
}

static void
fd_quic_tls_provide_data( fd_quic_tls_hs_t * tls_hs,
                          uint               enc_level,
                          uchar const *      msg,
                          ulong              msg_sz ) {
  FD_TEST( msg_sz<=FD_QUIC_TLS_RX_DATA_SZ );
  tls_hs->rx_enc_level = (uchar)enc_level;
  tls_hs->rx_sz        = (ushort)msg_sz;
  tls_hs->rx_off       = 0;
  fd_memcpy( tls_hs->rx_hs_buf, msg, msg_sz );
  fd_quic_tls_process( tls_hs );
}

int
main( int     argc,
      char ** argv ) {
  fd_boot( &argc, &argv );

  fd_rng_t  _rng[1];   fd_rng_t * rng    = fd_rng_join   ( fd_rng_new   ( _rng, 0U, 0UL ) );

  /* Generate certificate key */
  fd_tls_test_sign_ctx_t sign_ctx[1];
  fd_tls_test_sign_ctx( sign_ctx, rng );

  // config parameters
  fd_quic_tls_cfg_t cfg = {
    .secret_cb             = my_secrets,
    .handshake_complete_cb = my_hs_complete,
    .peer_params_cb        = my_transport_params,

    .max_concur_handshakes = 16,
    .cert_public_key       = sign_ctx->public_key,
    .signer                = fd_tls_test_sign( &sign_ctx ),
  };

  /* dump transport params */
  fd_quic_transport_params_t tmp_tp[1] = {0};
  FD_TEST( fd_quic_decode_transport_params( tmp_tp, test_tp, sizeof(test_tp)-1 )>=0 );

  fd_quic_dump_transport_params( tmp_tp, stdout );
  fflush( stdout );

  fd_quic_tls_t _quic_tls[1];
  fd_quic_tls_t * quic_tls = fd_quic_tls_new( _quic_tls, &cfg );
  FD_TEST( quic_tls );

  my_quic_tls_t    tls_client[1] = {0};
  fd_quic_tls_hs_t hs_client[1];
  FD_TEST( fd_quic_tls_hs_new(
      hs_client,
      quic_tls,
      tls_client,
      0 /* is_server */,
      tmp_tp,
      100UL ) );

  my_quic_tls_t    tls_server[1] = {0};
  fd_quic_tls_hs_t hs_server[1];
  FD_TEST( fd_quic_tls_hs_new(
      hs_server,
      quic_tls,
      tls_server,
      1 /* is_server */,
      tmp_tp,
      100UL ) );

  // generate initial secrets for client

  // server needs first packet with dst conn id in order to generate keys
  //   What happens when dst conn id changes?

  // start client handshake
  // client fd_quic_tls_hs_t is primed upon creation

  FD_LOG_INFO(( "entering main handshake loop" ));

  for( int l=0; l<16; ++l ) {
    FD_LOG_INFO(( "handshake loop step" ));

    int have_client_data = 0;
    int have_server_data = 0;

    // do we have data to transfer from client to server
    //while( tls_client->hs_data ) {
    while( 1 ) {
      //fd_hs_data_t * hs_data = tls_client->hs_data;
      fd_quic_tls_hs_data_t * hs_data   = NULL;
      for( uint j = 0; j < 4; ++j ) {
        hs_data = fd_quic_tls_get_hs_data( hs_client, j );
        if( hs_data ) break;
      }
      if( !hs_data ) break;

      FD_LOG_DEBUG(( "client hs_data: %p", (void*)hs_data ));
      FD_LOG_DEBUG(( "provide quic data client->server" ));

      // here we need encrypt/decrypt
      // collect fragments at the same enc/sec level, then encrypt
      // ... then decrypt and forward

      fd_quic_tls_provide_data( hs_server, hs_data->enc_level, hs_data->data, hs_data->data_sz );

      // remove hs_data from head of list
      //tls_client->hs_data = hs_data->next;
      fd_quic_tls_pop_hs_data( hs_client, hs_data->enc_level );

      // delete it
      //fd_hs_data_delete( hs_data );
    }

    // do we have data to transfer from server to client
    while( 1 ) {
      fd_quic_tls_hs_data_t * hs_data = NULL;
      for( uint j=0; j<4; ++j ) {
        hs_data = fd_quic_tls_get_hs_data( hs_server, j );
        if( hs_data ) break;
      }
      if( !hs_data ) break;

      FD_LOG_DEBUG(( "server hs_data: %p", (void *)hs_data ));
      FD_LOG_DEBUG(( "provide quic data server->client" ));

      // here we need encrypt/decrypt
      fd_quic_tls_provide_data( hs_client, hs_data->enc_level, hs_data->data, hs_data->data_sz );

      // remove hs_data from head of list
      fd_quic_tls_pop_hs_data( hs_server, hs_data->enc_level );
    }

    /* check for hs data here */
    have_client_data = 1;
    for( uint i=0; i<4; ++i ) {
      have_client_data &= (_Bool)fd_quic_tls_get_hs_data( hs_client, i );
    }

    have_server_data = 1;
    for( uint i=0; i<4; ++i ) {
      have_server_data &= (_Bool)fd_quic_tls_get_hs_data( hs_server, i );
    }

    if( tls_server->is_hs_complete && tls_client->is_hs_complete ) {
      FD_LOG_INFO(( "both handshakes complete" ));
      if( have_server_data ) {
        FD_LOG_INFO(( "tls_server still has data" ));
      }

      if( have_client_data ) {
        FD_LOG_INFO(( "tls_client still has data" ));
      }
      if( !( have_server_data || have_client_data ) ) break;
    }

    if( !( have_server_data || have_client_data ) ) {
      FD_LOG_INFO(( "incomplete, but no more data to exchange" ));
    }

  }

  FD_TEST( hs_server->hs.base.state == FD_TLS_HS_CONNECTED );
  FD_TEST( hs_client->hs.base.state == FD_TLS_HS_CONNECTED );
  FD_LOG_INFO(( "connected" ));

           fd_quic_tls_hs_delete( hs_client );
           fd_quic_tls_hs_delete( hs_server );
  FD_TEST( fd_quic_tls_delete   ( quic_tls  ) );

  fd_rng_delete( fd_rng_leave( rng ) );

  FD_LOG_NOTICE(( "pass" ));
  fd_halt();
  return 0;
}
