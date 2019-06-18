//  Copyright (C) 2015-2019 Virgil Security, Inc.
//
//  All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//      (1) Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//      (2) Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in
//      the documentation and/or other materials provided with the
//      distribution.
//
//      (3) Neither the name of the copyright holder nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR
//  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
//  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//  DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
//  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
//  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
//  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
//  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
//  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//  POSSIBILITY OF SUCH DAMAGE.
//
//  Lead Maintainer: Virgil Security Inc. <support@virgilsecurity.com>

#include <stdlib-config.h>
#include <logger-config.h>
#include <trust_list-config.h>

#include <tl_structs.h>
#include <trust_list.h>
#include "private/tl_operations.h"
#include "secbox.h"

static vs_tl_context_t _tl_static_ctx;

static vs_tl_context_t _tl_dynamic_ctx;

static vs_tl_context_t _tl_tmp_ctx;

/******************************************************************************/
// static bool
//_verify_hash(vsc_buffer_t *hash,
//             vscf_alg_id_t hash_type,
//             uint8_t pub_key[PUBKEY_TINY_ID_SZ],
//             uint8_t sign[SIGNATURE_SZ]) {
//
//    const uint8_t *mbedtls_sign;
//    size_t mbedtls_sign_sz;
//
//    uint8_t full_public_key[VIRGIL_PUBLIC_KEY_MAX_SIZE];
//    size_t full_public_key_sz = VIRGIL_PUBLIC_KEY_MAX_SIZE;
//
//    uint8_t full_signature[VIRGIL_SIGNATURE_MAX_SIZE];
//    size_t full_signature_sz = VIRGIL_SIGNATURE_MAX_SIZE;
//
//    // TODO: we need to choose a suitable converter, which depends on the signature type
//    if (!tiny_nist256_pubkey_to_virgil((uint8_t *)public_key, full_public_key, &full_public_key_sz))
//        return false;
//    if (!tiny_nist256_sign_to_virgil((uint8_t *)signature, full_signature, &full_signature_sz))
//        return false;
//
//    return virgil_sign_to_mbedtls(full_signature, full_signature_sz, &mbedtls_sign, &mbedtls_sign_sz) &&
//           ecdsa_verify_with_internal_key(full_public_key,
//                                          full_public_key_sz,
//                                          hash_type,
//                                          hash,
//                                          (size_t)hash_size(hash_type),
//                                          mbedtls_sign,
//                                          mbedtls_sign_sz,
//                                          VS_SIGN_COMMON);
//}

/******************************************************************************/

// static bool
//_verify_tl_signature(vsc_buffer_t *hash,
//                     vscf_alg_id_t hash_type,
//                     iot_hsm_element_e el,
//                     crypto_signature_t *sign,
//                     vscf_alg_id_t sign_type) {
//    crypto_signed_hl_public_key_t key;
//    size_t read_sz;
//    vs_secbox_element_info_t info;
//    info.id = el;
//
//    for (size_t i = 0; i < PROVISION_KEYS_QTY; ++i) {
//        info.index = i;
//
//        if (TL_OK == vs_secbox_load(&info, (uint8_t *)&key, sizeof(crypto_signed_hl_public_key_t), &read_sz) &&
//            key.public_key.id.key_id == sign->signer_id.key_id &&
//            keystorage_verify_hl_key_sign((uint8_t *)&key, sizeof(key))) {
//            if (_verify_hash(hash, hash_type, key.public_key.val, sign->val)) {
//                return true;
//            }
//        }
//    }
//    return false;
//}

/******************************************************************************/
// static bool
//_verify_tl_signatures(vsc_buffer_t *hash,
//                      vscf_alg_id_t hash_type,
//                      crypto_signature_t signs[TL_SIGNATURES_QTY],
//                      vscf_alg_id_t sign_type) {
//
//    if (!_verify_tl_signature(hash, hash_type, VS_SECBOX_ELEMENT_PBA, &signs[0], sign_type) ||
//        !_verify_tl_signature(hash, hash_type, IOT_HSM_ELEMENT_PBT, &signs[1], sign_type)) {
//        return false;
//    }
//    return true;
//}

/******************************************************************************/
static bool
_verify_tl(vs_tl_context_t *tl_ctx) {
    vs_tl_header_t header;
    vs_tl_pubkey_t key;
    vs_tl_footer_t footer;
    uint16_t i;


    //    uint8_t buf[32];
    //    vscf_sha256_t hash_ctx;
    //    vsc_buffer_t hash;
    //
    tl_ctx->ready = true;
    if (TL_OK != vs_tl_header_load(tl_ctx->storage.storage_type, &header)) {
        tl_ctx->ready = false;
        return false;
    }

    uint32_t tl_size = header.pub_keys_count * sizeof(vs_tl_pubkey_t) + sizeof(vs_tl_header_t) + sizeof(vs_tl_footer_t);

    if (header.tl_size > TL_STORAGE_SIZE || header.tl_size != tl_size) {
        tl_ctx->ready = false;
        return false;
    }
    //
    //    vscf_sha256_init(&hash_ctx);
    //    vsc_buffer_init(&hash);
    //    vsc_buffer_use(&hash, buf, sizeof(buf));
    //    vscf_sha256_start(&hash_ctx);
    //    vscf_sha256_update(&hash_ctx, vsc_data((uint8_t *)&header, sizeof(trust_list_header_t)));
    //
    for (i = 0; i < header.pub_keys_count; ++i) {

        if (TL_OK != vs_tl_key_load(tl_ctx->storage.storage_type, i, &key)) {
            tl_ctx->ready = false;
            return false;
        }
        //            vscf_sha256_update(&hash_ctx, vsc_data((uint8_t *)&key, sizeof(trust_list_pub_key_t)));
    }
    //
    //    vscf_sha256_finish(&hash_ctx, &hash);
    //
    bool res = (TL_OK == vs_tl_footer_load(tl_ctx->storage.storage_type, &footer) /*&&
                    _verify_tl_signatures(&hash, vscf_sha256_alg_id(&hash_ctx), &footer.auth_sign,
                    vscf_alg_id_SECP256R1)*/);
    if (!res) {
        tl_ctx->ready = false;
    }
    //
    //    vscf_sha256_cleanup(&hash_ctx);

    return true;
}

/******************************************************************************/
static void
_init_tl_ctx(size_t storage_type, vs_tl_context_t *ctx) {
    if (!ctx)
        return;

    memset(&ctx->keys_qty, 0, sizeof(tl_keys_qty_t));
    ctx->ready = false;
    ctx->storage.storage_type = storage_type;
}

/******************************************************************************/
static vs_tl_context_t *
_get_tl_ctx(size_t storage_type) {
    switch (storage_type) {
    case TL_STORAGE_TYPE_STATIC:
        return &_tl_static_ctx;
    case TL_STORAGE_TYPE_DYNAMIC:
        return &_tl_dynamic_ctx;
    case TL_STORAGE_TYPE_TMP:
        return &_tl_tmp_ctx;
    default:
        break;
    }
    return NULL;
}

/******************************************************************************/
static int
_copy_tl_file(vs_tl_context_t *dst, vs_tl_context_t *src) {
    vs_tl_header_t header;
    vs_tl_pubkey_t key;
    vs_tl_footer_t footer;
    uint16_t i;

    if (!src->ready) {
        return TL_ERROR_GENERAL;
    }

    if (TL_OK != vs_tl_header_load(src->storage.storage_type, &header) ||
        TL_OK != vs_tl_header_save(dst->storage.storage_type, &header)) {
        dst->ready = false;
        return TL_ERROR_WRITE;
    }

    for (i = 0; i < header.pub_keys_count; ++i) {
        if (TL_OK != vs_tl_key_load(src->storage.storage_type, i, &key) ||
            TL_OK != vs_tl_key_save(dst->storage.storage_type, &key)) {
            dst->ready = false;
            return TL_ERROR_WRITE;
        }
    }

    if (TL_OK != vs_tl_footer_load(src->storage.storage_type, &footer) ||
        TL_OK != vs_tl_footer_save(dst->storage.storage_type, &footer)) {
        dst->ready = false;
        return TL_ERROR_WRITE;
    }

    dst->ready = true;
    dst->keys_qty.keys_amount = src->keys_qty.keys_amount;
    dst->keys_qty.keys_count = src->keys_qty.keys_count;

    return TL_OK;
}

/******************************************************************************/
bool
vs_tl_verify_hl_key(const uint8_t *key_to_check, size_t key_size) {

    //    size_t read_sz;
    //
    //    if (!key_to_check || sizeof(crypto_signed_hl_public_key_t) != key_size) {
    //        return false;
    //    }
    //
    //    crypto_signed_hl_public_key_t *key = (crypto_signed_hl_public_key_t *)key_to_check;
    //    crypto_signed_hl_public_key_t rec_key;
    //
    //    uint8_t buf[32];
    //    vsc_buffer_t hash;
    //    vsc_buffer_init(&hash);
    //    vsc_buffer_use(&hash, buf, sizeof(buf));
    //
    //    vscf_sha256_hash(vsc_data(key->public_key.val, PUBKEY_TINY_SZ), &hash);
    //
    //    for (size_t i = 0; i < PROVISION_KEYS_QTY; ++i) {
    //        vs_secbox_element_info_t el = {IOT_HSM_ELEMENT_PBR, i};
    //
    //        if (GATEWAY_OK == vs_secbox_load(&el, (uint8_t *)&rec_key, sizeof(crypto_signed_hl_public_key_t),
    //        &read_sz) &&
    //            key->sign.signer_id.key_id == rec_key.public_key.id.key_id &&
    //            _verify_hash(&hash, vscf_alg_id_SHA256, rec_key.public_key.val, key->sign.val)) {
    //            return true;
    //        }
    //    }
    return false;
}

/******************************************************************************/
void
vs_tl_storage_init() {

    _init_tl_ctx(TL_STORAGE_TYPE_DYNAMIC, &_tl_dynamic_ctx);
    _init_tl_ctx(TL_STORAGE_TYPE_STATIC, &_tl_static_ctx);
    _init_tl_ctx(TL_STORAGE_TYPE_TMP, &_tl_tmp_ctx);

    if (!_verify_tl(&_tl_dynamic_ctx) && _verify_tl(&_tl_static_ctx)) {
        if (TL_OK == _copy_tl_file(&_tl_dynamic_ctx, &_tl_static_ctx)) {
            _verify_tl(&_tl_dynamic_ctx);
        }
    }
}
/******************************************************************************/
int
vs_tl_header_save(size_t storage_type, const vs_tl_header_t *header) {
    vs_tl_context_t *tl_ctx = _get_tl_ctx(storage_type);
    vs_secbox_element_info_t el = {storage_type, VS_TL_ELEMENT_TLH, 0};

    if (!header || NULL == tl_ctx) {
        return TL_ERROR_PARAMS;
    }

    uint32_t tl_size =
            header->pub_keys_count * sizeof(vs_tl_pubkey_t) + sizeof(vs_tl_header_t) + sizeof(vs_tl_footer_t);

    if (header->tl_size > TL_STORAGE_SIZE || header->tl_size != tl_size) {
        return TL_ERROR_SMALL_BUFFER;
    }

    tl_ctx->ready = false;
    tl_ctx->keys_qty.keys_count = 0;
    tl_ctx->keys_qty.keys_amount = header->pub_keys_count;

    if (0 == vs_secbox_save(&el, (uint8_t *)header, sizeof(vs_tl_header_t))) {
        return TL_OK;
    }

    return TL_ERROR_WRITE;
}

/******************************************************************************/
int
vs_tl_header_load(size_t storage_type, vs_tl_header_t *header) {
    vs_tl_context_t *tl_ctx = _get_tl_ctx(storage_type);
    size_t readed_sz;
    vs_secbox_element_info_t el = {storage_type, VS_TL_ELEMENT_TLH, 0};

    if (NULL == tl_ctx) {
        return TL_ERROR_PARAMS;
    }

    if (!tl_ctx->ready) {
        return TL_ERROR_GENERAL;
    }

    if (0 == vs_secbox_load(&el, (uint8_t *)header, sizeof(vs_tl_header_t))) {
        return TL_OK;
    }

    return TL_ERROR_READ;
}

/******************************************************************************/
int
vs_tl_footer_save(size_t storage_type, const vs_tl_footer_t *footer) {
    vs_tl_context_t *tl_ctx = _get_tl_ctx(storage_type);
    vs_secbox_element_info_t el = {storage_type, VS_TL_ELEMENT_TLF, 0};

    if (NULL == tl_ctx || tl_ctx->keys_qty.keys_amount != tl_ctx->keys_qty.keys_count) {
        return TL_ERROR_PARAMS;
    }

    if (0 == vs_secbox_save(&el, (uint8_t *)footer, sizeof(vs_tl_footer_t))) {
        return TL_OK;
    }

    return TL_ERROR_WRITE;
}

/******************************************************************************/
int
vs_tl_footer_load(size_t storage_type, vs_tl_footer_t *footer) {
    vs_tl_context_t *tl_ctx = _get_tl_ctx(storage_type);
    vs_secbox_element_info_t el = {storage_type, VS_TL_ELEMENT_TLF, 0};

    if (NULL == tl_ctx) {
        return TL_ERROR_PARAMS;
    }

    if (!tl_ctx->ready) {
        return TL_ERROR_GENERAL;
    }

    if (0 == vs_secbox_load(&el, (uint8_t *)footer, sizeof(vs_tl_footer_t))) {
        return TL_OK;
    }
    return TL_ERROR_READ;
}

/******************************************************************************/
int
vs_tl_key_save(size_t storage_type, const vs_tl_pubkey_t *key) {
    vs_tl_context_t *tl_ctx = _get_tl_ctx(storage_type);
    vs_secbox_element_info_t el = {storage_type, VS_TL_ELEMENT_TLC, 0};

    if (NULL == tl_ctx) {
        return TL_ERROR_PARAMS;
    }

    if (tl_ctx->keys_qty.keys_count >= tl_ctx->keys_qty.keys_amount) {
        tl_ctx->keys_qty.keys_count = tl_ctx->keys_qty.keys_amount;
        return TL_ERROR_WRITE;
    }

    el.index = tl_ctx->keys_qty.keys_count;
    if (0 != vs_secbox_save(&el, (uint8_t *)key, sizeof(vs_tl_pubkey_t))) {
        return TL_ERROR_WRITE;
    }

    tl_ctx->keys_qty.keys_count++;
    return TL_OK;
}

/******************************************************************************/
int
vs_tl_key_load(size_t storage_type, vs_tl_key_handle handle, vs_tl_pubkey_t *key) {
    vs_tl_context_t *tl_ctx = _get_tl_ctx(storage_type);
    vs_secbox_element_info_t el = {storage_type, VS_TL_ELEMENT_TLC, handle};

    if (NULL == tl_ctx) {
        return TL_ERROR_PARAMS;
    }

    if (!tl_ctx->ready) {
        return TL_ERROR_GENERAL;
    }

    if (0 == vs_secbox_load(&el, (uint8_t *)key, sizeof(vs_tl_pubkey_t))) {
        return TL_OK;
    }

    return TL_ERROR_READ;
}

/******************************************************************************/
int
vs_tl_invalidate(size_t storage_type) {
    vs_tl_header_t header;
    vs_secbox_element_info_t el = {storage_type, VS_TL_ELEMENT_TLH, 0};

    vs_tl_context_t *tl_ctx = _get_tl_ctx(storage_type);

    if (NULL == tl_ctx) {
        return TL_ERROR_PARAMS;
    }

    tl_ctx->keys_qty.keys_count = 0;
    tl_ctx->keys_qty.keys_amount = 0;

    if (TL_OK != vs_tl_header_load(storage_type, &header) || (0 != vs_secbox_del(&el))) {
        return TL_OK;
    }

    tl_ctx->ready = false;

    el.id = VS_TL_ELEMENT_TLF;
    if (0 != vs_secbox_del(&el)) {
        return TL_OK;
    }

    el.id = VS_TL_ELEMENT_TLC;
    for (el.index = 0; el.index < header.pub_keys_count; ++el.index) {
        if (0 != vs_secbox_del(&el)) {
            return TL_OK;
        }
    }

    return TL_OK;
}

/******************************************************************************/
int
vs_tl_apply_tmp_to(size_t storage_type) {
    vs_tl_context_t *tl_ctx = _get_tl_ctx(storage_type);

    if (NULL == tl_ctx) {
        return TL_ERROR_PARAMS;
    }

    if (_verify_tl(&_tl_tmp_ctx)) {
        if (TL_OK != vs_tl_invalidate(storage_type)) {
            return TL_ERROR_GENERAL;
        }

        return _copy_tl_file(tl_ctx, &_tl_tmp_ctx);
    }

    return TL_ERROR_GENERAL;
}
