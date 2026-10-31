// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
#pragma once
// The HTTPS server's identity (the phone page's mic needs https). Every Oreo makes its OWN on first boot: an ECDSA
// P-256 key and a self-signed certificate (CN=oreo.local, valid 20 years), kept in flash (Preferences "tls") - no
// shared private key in the source. The browser warns once ("not private"): accept it for this robot.
#include <Preferences.h>
#include "mbedtls/pk.h"
#include "mbedtls/ecp.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "esp_heap_caps.h"

namespace tlsid {

static char* certPem = nullptr;
static char* keyPem = nullptr;

static bool make(char* cert, size_t certN, char* key, size_t keyN) {
  mbedtls_pk_context pk;
  mbedtls_entropy_context ent;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_x509write_cert crt;
  mbedtls_mpi serial;
  mbedtls_pk_init(&pk);
  mbedtls_entropy_init(&ent);
  mbedtls_ctr_drbg_init(&drbg);
  mbedtls_x509write_crt_init(&crt);
  mbedtls_mpi_init(&serial);
  const char* pers = "oreo-tls";
  bool ok = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent, (const unsigned char*)pers, strlen(pers)) == 0 &&
            mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) == 0 &&
            mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk), mbedtls_ctr_drbg_random, &drbg) == 0 &&
            mbedtls_pk_write_key_pem(&pk, (unsigned char*)key, keyN) == 0;
  if (ok) {
    uint8_t sn[8];
    mbedtls_ctr_drbg_random(&drbg, sn, sizeof(sn));
    sn[0] &= 0x7F;
    mbedtls_mpi_read_binary(&serial, sn, sizeof(sn));
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &pk);
    mbedtls_x509write_crt_set_issuer_key(&crt, &pk);
    ok = mbedtls_x509write_crt_set_subject_name(&crt, "CN=oreo.local,O=Oreo robot") == 0 &&
         mbedtls_x509write_crt_set_issuer_name(&crt, "CN=oreo.local,O=Oreo robot") == 0 &&
         mbedtls_x509write_crt_set_serial(&crt, &serial) == 0 &&
         mbedtls_x509write_crt_set_validity(&crt, "20250101000000", "20450101000000") == 0 &&
         mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1) == 0 &&
         mbedtls_x509write_crt_pem(&crt, (unsigned char*)cert, certN, mbedtls_ctr_drbg_random, &drbg) == 0;
  }
  mbedtls_mpi_free(&serial);
  mbedtls_x509write_crt_free(&crt);
  mbedtls_pk_free(&pk);
  mbedtls_ctr_drbg_free(&drbg);
  mbedtls_entropy_free(&ent);
  return ok;
}

// the PEMs (NUL-terminated; the lengths include the NUL, as the https server wants) - made once, then loaded
inline bool get(const uint8_t** cert, size_t* certLen, const uint8_t** key, size_t* keyLen) {
  if (!certPem) {
    const size_t CN = 1200, KN = 400;
    certPem = (char*)heap_caps_calloc(1, CN, MALLOC_CAP_SPIRAM);
    keyPem = (char*)heap_caps_calloc(1, KN, MALLOC_CAP_SPIRAM);
    if (!certPem || !keyPem) return false;
    Preferences p;
    p.begin("tls", false);
    const size_t cl = p.getBytesLength("crt"), kl = p.getBytesLength("key");
    bool have = cl > 100 && cl < CN && kl > 100 && kl < KN && p.getBytes("crt", certPem, cl) == cl && p.getBytes("key", keyPem, kl) == kl;
    if (!have) {
      have = make(certPem, CN, keyPem, KN);
      if (have) {
        p.putBytes("crt", certPem, strlen(certPem) + 1);
        p.putBytes("key", keyPem, strlen(keyPem) + 1);
      }
    }
    p.end();
    if (!have) return false;
  }
  *cert = (const uint8_t*)certPem;
  *certLen = strlen(certPem) + 1;
  *key = (const uint8_t*)keyPem;
  *keyLen = strlen(keyPem) + 1;
  return true;
}

}  // namespace tlsid
