#ifndef TL_STRUCTS_H
#define TL_STRUCTS_H

#include <stdint.h>
#include <stdbool.h>

#define PUBKEY_TINY_SZ (64)
#define PUBKEY_TINY_ID_SZ (2)
#define SIGNATURE_SZ (64)
#define TL_SIGNATURES_QTY (2)

#define TL_STORAGE_TYPE_STATIC 0
#define TL_STORAGE_TYPE_DYNAMIC 1
#define TL_STORAGE_TYPE_TMP 2

typedef enum {
    TL_OK = 0,
    TL_ERROR_GENERAL = -1,
    TL_ERROR_PARAMS = -2,
    TL_ERROR_SMALL_BUFFER = -3,
    TL_ERROR_WRITE = -4,
    TL_ERROR_READ = -5,
} TL_RESULT;

typedef struct __attribute__((__packed__)) {
    size_t storage_type;
} vs_tl_storage_ctx_t;

typedef struct __attribute__((__packed__)) {
    uint16_t keys_amount;
    uint16_t keys_count;
} tl_keys_qty_t;

typedef struct __attribute__((__packed__)) {
    bool ready;
    vs_tl_storage_ctx_t storage;
    tl_keys_qty_t keys_qty;
} vs_tl_context_t;

typedef size_t vs_tl_key_handle;

typedef struct __attribute__((__packed__)) {
    union {
        uint8_t val[PUBKEY_TINY_ID_SZ];
        uint16_t key_id;
    };
} vs_crypto_public_key_id_t;

typedef struct __attribute__((__packed__)) {
    vs_crypto_public_key_id_t signer_id;
    uint8_t val[SIGNATURE_SZ];
} vs_crypto_signature_t;

typedef struct __attribute__((__packed__)) {
    uint32_t tl_size;
    uint16_t version;
    uint16_t pub_keys_count;
    uint8_t reserved[24];
} vs_tl_header_t;

typedef struct __attribute__((__packed__)) {
    vs_crypto_signature_t auth_sign;
    vs_crypto_signature_t tl_service_sign;
    uint8_t tl_type;
    uint8_t reserved[32];
} vs_tl_footer_t;

typedef struct __attribute__((__packed__)) {
    vs_crypto_public_key_id_t id;
    uint16_t type;
    uint8_t reserved[28];
} vs_tl_pubkey_meta_t;

typedef struct __attribute__((__packed__)) {
    uint8_t val[PUBKEY_TINY_SZ];
    vs_tl_pubkey_meta_t meta;
} vs_tl_pubkey_t;

typedef struct __attribute__((__packed__)) {
    vs_crypto_public_key_id_t id;
    uint8_t val[PUBKEY_TINY_SZ];
} vs_crypto_hl_public_key_t;

typedef struct __attribute__((__packed__)) {
    vs_crypto_hl_public_key_t public_key;
    vs_crypto_signature_t sign;
} vs_crypto_signed_hl_public_key_t;

#endif // TL_STRUCTS_H
