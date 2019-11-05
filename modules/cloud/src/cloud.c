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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <virgil/iot/cloud/private/cloud_include.h>

#define MAX_EP_SIZE (256)

static const vs_cloud_impl_t *_hal_impl = NULL;
static vs_hsm_impl_t *_hsm = NULL;

/******************************************************************************/
static bool
_data_to_hex(const uint8_t *_data, uint32_t _len, uint8_t *_out_data, uint32_t *_in_out_len) {
    const uint8_t hex_str[] = "0123456789abcdef";

    if (!_len) {
        return false;
    }

    if (*_in_out_len < _len * 2 + 1) {
        return false;
    }

    *_in_out_len = _len * 2 + 1;
    _out_data[*_in_out_len - 1] = 0;
    size_t i;

    for (i = 0; i < _len; i++) {
        _out_data[i * 2 + 0] = hex_str[(_data[i] >> 4) & 0x0F];
        _out_data[i * 2 + 1] = hex_str[(_data[i]) & 0x0F];
    }
    return true;
}

/******************************************************************************/
static void
_get_serial_number_in_hex_str(char _out_str[VS_DEVICE_SERIAL_SIZE * 2 + 1]) {
    vs_device_serial_t serial_number;
    uint32_t _in_out_len = VS_DEVICE_SERIAL_SIZE * 2 + 1;
    vs_impl_device_serial(serial_number);
    _data_to_hex((uint8_t *)serial_number, VS_DEVICE_SERIAL_SIZE, (uint8_t *)_out_str, &_in_out_len);
}

/******************************************************************************/
static vs_status_e
_decrypt_answer(char *out_answer, size_t *in_out_answer_len) {
    jobj_t jobj;
    size_t buf_size = *in_out_answer_len;
    int crypto_answer_b64_len;

    CHECK_NOT_ZERO_RET(_hsm, VS_CODE_ERR_NULLPTR_ARGUMENT);

    if (json_parse_start(&jobj, out_answer, buf_size) != VS_JSON_ERR_OK) {
        return VS_CODE_ERR_JSON;
    }

    char *crypto_answer_b64 = (char *)VS_IOT_MALLOC(buf_size);
    CHECK(crypto_answer_b64, "No memory to allocate %lu bytes for an answer", buf_size);

    if (json_get_val_str(&jobj, "encrypted_value", crypto_answer_b64, (int)buf_size) != VS_JSON_ERR_OK) {
        goto terminate;
    } else {
        crypto_answer_b64_len = base64decode_len(crypto_answer_b64, (int)strlen(crypto_answer_b64));

        if (0 >= crypto_answer_b64_len || crypto_answer_b64_len > buf_size) {
            goto terminate;
        }

        base64decode(crypto_answer_b64,
                     (int)strlen(crypto_answer_b64),
                     (uint8_t *)crypto_answer_b64,
                     &crypto_answer_b64_len);
        size_t decrypted_data_sz;

        if (VS_CODE_OK != _hsm->ecies_decrypt(NULL,
                                              0,
                                              (uint8_t *)crypto_answer_b64,
                                              (size_t)crypto_answer_b64_len,
                                              (uint8_t *)out_answer,
                                              buf_size,
                                              &decrypted_data_sz) ||
            decrypted_data_sz > UINT16_MAX) {
            goto terminate;
        }
        *in_out_answer_len = (uint16_t)decrypted_data_sz;
        out_answer[*in_out_answer_len] = '\0';
    }
    json_parse_stop(&jobj);
    VS_IOT_FREE(crypto_answer_b64);
    return VS_CODE_OK;

terminate:
    json_parse_stop(&jobj);
    if (crypto_answer_b64) {
        VS_IOT_FREE(crypto_answer_b64);
    }

    *in_out_answer_len = 0;
    out_answer[0] = '\0';
    return VS_CODE_ERR_JSON;
}

/******************************************************************************/
static vs_status_e
_get_credentials(const char *host, char *ep, char *id, char *out_answer, size_t *in_out_answer_len) {
    vs_status_e ret;
    char serial[VS_DEVICE_SERIAL_SIZE * 2 + 1];

    CHECK_NOT_ZERO_RET(out_answer, VS_CODE_ERR_NULLPTR_ARGUMENT);
    CHECK_NOT_ZERO_RET(in_out_answer_len, VS_CODE_ERR_NULLPTR_ARGUMENT);
    CHECK_NOT_ZERO_RET(_hal_impl, VS_CODE_ERR_NOINIT);
    CHECK_NOT_ZERO_RET(_hal_impl->http_get, VS_CODE_ERR_NOINIT);

    char *url = (char *)VS_IOT_MALLOC(MAX_EP_SIZE);

    _get_serial_number_in_hex_str(serial);

    int res = VS_IOT_SNPRINTF(url, MAX_EP_SIZE, "%s/%s/%s/%s", host, ep, serial, id);
    if (res < 0 || res > MAX_EP_SIZE ||
        VS_CODE_OK != _hal_impl->http_get(url, out_answer, NULL, NULL, in_out_answer_len)) {
        ret = VS_CODE_ERR_CLOUD;
    } else {
        ret = _decrypt_answer(out_answer, in_out_answer_len);
    }

    VS_IOT_FREE(url);
    return ret;
}

/******************************************************************************/
vs_status_e
vs_cloud_init(const vs_cloud_impl_t *cloud_impl,
              const vs_cloud_message_bin_impl_t *message_bin_impl,
              vs_hsm_impl_t *hsm) {
    CHECK_NOT_ZERO_RET(hsm, VS_CODE_ERR_NULLPTR_ARGUMENT);
    CHECK_NOT_ZERO_RET(hsm->ecies_decrypt, VS_CODE_ERR_NULLPTR_ARGUMENT);
    CHECK_NOT_ZERO_RET(cloud_impl, VS_CODE_ERR_NULLPTR_ARGUMENT);
    CHECK_NOT_ZERO_RET(cloud_impl->http_get, VS_CODE_ERR_NULLPTR_ARGUMENT);

    _hal_impl = cloud_impl;
    _hsm = hsm;
    return vs_cloud_message_bin_init(message_bin_impl);
}

// TODO: Will be used for alexa
#if 0
/******************************************************************************/
vs_status_e
vs_cloud_fetch_amazon_credentials(char *out_answer, size_t *in_out_answer_len) {
    return _get_credentials(VS_CLOUD_HOST, VS_THING_EP, VS_AWS_ID, out_answer, in_out_answer_len);
}
#endif

/******************************************************************************/
vs_status_e
vs_cloud_fetch_message_bin_credentials(const char *cloud_host, char *out_answer, size_t *in_out_answer_len) {
    CHECK_NOT_ZERO_RET(cloud_host, VS_CODE_ERR_NULLPTR_ARGUMENT);
    return _get_credentials(cloud_host, VS_THING_EP, VS_MQTT_ID, out_answer, in_out_answer_len);
}

#define VS_CLOUD_FETCH_FW_STEP_HEADER 0
#define VS_CLOUD_FETCH_FW_STEP_CHUNKS 1
#define VS_CLOUD_FETCH_FW_STEP_FOOTER 2
#define VS_CLOUD_FETCH_FW_STEP_DONE 3

typedef struct {
    uint8_t step;
    bool is_descriptor_stored;
    vs_firmware_header_t header;
    uint32_t file_offset;
    uint16_t chunks_qty;
    uint16_t chunk_cnt;
    uint8_t *buff;
    uint32_t buff_sz;
    size_t footer_sz;
    size_t used_size;
} fw_resp_buff_t;

/*************************************************************************/
static bool
_check_firmware_header(vs_firmware_header_t *header) {
    return header->descriptor.firmware_length == header->code_length &&
           header->code_offset == sizeof(vs_firmware_header_t) && header->footer_offset >= header->code_length &&
           header->footer_offset + header->footer_length < VS_MAX_FIRMWARE_UPDATE_SIZE;
}

/*************************************************************************/
static size_t
_store_fw_handler(char *contents, size_t chunksize, void *userdata) {
    fw_resp_buff_t *resp = (fw_resp_buff_t *)userdata;
    size_t rest_data_sz = chunksize;
    vs_firmware_descriptor_t old_desc;

    if (NULL == resp->buff) {
        return 0;
    }

    switch (resp->step) {
    case VS_CLOUD_FETCH_FW_STEP_HEADER: {
        size_t read_sz = chunksize;

        if (resp->used_size + chunksize > sizeof(vs_firmware_header_t)) {
            read_sz = sizeof(vs_firmware_header_t) - resp->used_size;
        }

        VS_IOT_MEMCPY(&((uint8_t *)&resp->header)[resp->used_size], contents, read_sz);

        resp->used_size += read_sz;

        if (resp->used_size == sizeof(vs_firmware_header_t)) {

            vs_firmware_ntoh_header(&resp->header);

            if (!_check_firmware_header(&resp->header) ||
                VS_CODE_OK != vs_cloud_is_new_firmware_version_available(&resp->header.descriptor)) {
                return 0;
            }

            // Remove old version from fw storage
            if (VS_CODE_OK == vs_firmware_load_firmware_descriptor(resp->header.descriptor.info.manufacture_id,
                                                                   resp->header.descriptor.info.device_type,
                                                                   &old_desc)) {
                vs_firmware_delete_firmware(&old_desc);
            }

            if (VS_CODE_OK != vs_firmware_save_firmware_descriptor(&resp->header.descriptor)) {
                return 0;
            }
            resp->is_descriptor_stored = true;

            resp->chunks_qty = resp->header.code_length / resp->header.descriptor.chunk_size;
            if (resp->header.code_length % resp->header.descriptor.chunk_size) {
                resp->chunks_qty++;
            }

            if (resp->header.descriptor.chunk_size > resp->buff_sz) {
                VS_IOT_FREE(resp->buff);
                resp->buff = VS_IOT_CALLOC(1, resp->header.descriptor.chunk_size);
                resp->buff_sz = resp->header.descriptor.chunk_size;
            }

            resp->step = VS_CLOUD_FETCH_FW_STEP_CHUNKS;
        }

        if (read_sz == chunksize) {
            break;
        }

        contents += read_sz;
        rest_data_sz = chunksize - read_sz;
        resp->used_size = 0;
    }
    case VS_CLOUD_FETCH_FW_STEP_CHUNKS:
        while (rest_data_sz && VS_CLOUD_FETCH_FW_STEP_CHUNKS == resp->step) {
            size_t read_sz = rest_data_sz;

            uint32_t fw_rest = resp->header.code_length - resp->file_offset;
            uint32_t required_chunk_size =
                    fw_rest > resp->header.descriptor.chunk_size ? resp->header.descriptor.chunk_size : fw_rest;

            if (resp->used_size + rest_data_sz > required_chunk_size) {
                read_sz = required_chunk_size - resp->used_size;
            }

            VS_IOT_MEMCPY(&(resp->buff[resp->used_size]), contents, read_sz);

            contents += read_sz;
            rest_data_sz -= read_sz;
            resp->used_size += read_sz;

            if (resp->used_size == required_chunk_size) {
                resp->used_size = 0;
                if (VS_CODE_OK !=
                    vs_firmware_save_firmware_chunk(
                            &resp->header.descriptor, resp->buff, required_chunk_size, resp->file_offset)) {
                    return 0;
                }

                resp->file_offset += required_chunk_size;

                if (++resp->chunk_cnt == resp->chunks_qty) {

                    if (resp->header.footer_length > resp->buff_sz) {
                        VS_IOT_FREE(resp->buff);
                        resp->buff = VS_IOT_CALLOC(1, resp->header.footer_length);
                        resp->buff_sz = resp->header.footer_length;
                    }

                    resp->step = VS_CLOUD_FETCH_FW_STEP_FOOTER;
                }
            }
        }

        if (!rest_data_sz) {
            break;
        }

    case VS_CLOUD_FETCH_FW_STEP_FOOTER:

        if (resp->used_size + rest_data_sz > resp->header.footer_length) {
            return 0;
        }

        VS_IOT_MEMCPY(&(resp->buff[resp->used_size]), contents, rest_data_sz);
        resp->used_size += rest_data_sz;

        if (resp->used_size == resp->header.footer_length) {
            resp->used_size = 0;

            vs_firmware_descriptor_t f;
            VS_IOT_MEMCPY(&f, &((vs_firmware_footer_t *)resp->buff)->descriptor, sizeof(vs_firmware_descriptor_t));
            vs_firmware_ntoh_descriptor(&f);

            if (0 != memcmp(&resp->header.descriptor, &f, sizeof(vs_firmware_descriptor_t))) {
                VS_LOG_ERROR("Invalid firmware descriptor");
                return VS_CODE_ERR_INCORRECT_ARGUMENT;
            }

            if (VS_CODE_OK != vs_firmware_save_firmware_footer(&resp->header.descriptor, resp->buff)) {
                return 0;
            }
            resp->step = VS_CLOUD_FETCH_FW_STEP_DONE;
        }
        break;
    }
    return chunksize;
}

/*************************************************************************/
vs_status_e
vs_cloud_fetch_and_store_fw_file(const char *fw_file_url, vs_firmware_header_t *fetched_header) {
    vs_status_e res = VS_CODE_OK;
    CHECK_NOT_ZERO_RET(fw_file_url, VS_CODE_ERR_NULLPTR_ARGUMENT);
    CHECK_NOT_ZERO_RET(fetched_header, VS_CODE_ERR_NULLPTR_ARGUMENT);
    CHECK_NOT_ZERO_RET(_hal_impl, VS_CODE_ERR_NOINIT);
    CHECK_NOT_ZERO_RET(_hal_impl->http_get, VS_CODE_ERR_NOINIT);
    size_t in_out_answer_len = 0;
    fw_resp_buff_t resp;
    VS_IOT_MEMSET(&resp, 0, sizeof(resp));

    resp.buff = VS_IOT_CALLOC(1, sizeof(vs_firmware_header_t));
    resp.buff_sz = sizeof(vs_firmware_header_t);

    if (VS_CODE_OK != _hal_impl->http_get(fw_file_url, NULL, _store_fw_handler, &resp, &in_out_answer_len) ||
        VS_CLOUD_FETCH_FW_STEP_DONE != resp.step) {
        res = VS_CODE_ERR_CLOUD;

        if (resp.is_descriptor_stored) {
            vs_firmware_delete_firmware(&resp.header.descriptor);
        }

    } else {
        VS_IOT_MEMCPY(fetched_header, &resp.header, sizeof(vs_firmware_header_t));
    }

    VS_IOT_FREE(resp.buff);
    return res;
}

#define VS_CLOUD_FETCH_Tl_STEP_HEADER 0
#define VS_CLOUD_FETCH_Tl_STEP_KEYS 1
#define VS_CLOUD_FETCH_Tl_STEP_FOOTER 2
#define VS_CLOUD_FETCH_Tl_STEP_DONE 3

typedef struct {
    uint8_t step;
    vs_tl_header_t header;
    vs_tl_header_t host_header;
    uint16_t keys_cnt;
    uint8_t *buff;
    size_t footer_sz;
    size_t used_size;
} tl_resp_buff_t;

/*************************************************************************/
static size_t
_store_tl_handler(char *contents, size_t chunksize, void *userdata) {
    tl_resp_buff_t *resp = (tl_resp_buff_t *)userdata;
    size_t rest_data_sz = chunksize;
    if (NULL == resp->buff) {
        return 0;
    }

    switch (resp->step) {
    case VS_CLOUD_FETCH_Tl_STEP_HEADER: {
        size_t read_sz = chunksize;

        if (resp->used_size + chunksize > sizeof(vs_tl_header_t)) {
            read_sz = sizeof(vs_tl_header_t) - resp->used_size;
        }

        VS_IOT_MEMCPY(&((uint8_t *)&resp->header)[resp->used_size], contents, read_sz);

        resp->used_size += read_sz;

        if (resp->used_size == sizeof(vs_tl_header_t)) {

            vs_tl_header_to_host(&resp->header, &resp->host_header);

            uint32_t required_storage_sz = (resp->host_header.pub_keys_count + 2) * VS_TL_STORAGE_MAX_PART_SIZE;

            if (resp->host_header.tl_size > VS_TL_STORAGE_SIZE || required_storage_sz > VS_TL_STORAGE_SIZE) {
                return 0;
            }

            vs_tl_element_info_t info = {.id = VS_TL_ELEMENT_TLH, .index = 0};

            if (VS_CODE_OK != vs_tl_save_part(&info, (uint8_t *)&resp->header, sizeof(vs_tl_header_t))) {
                return 0;
            }
            resp->step = VS_CLOUD_FETCH_Tl_STEP_KEYS;
        }

        if (read_sz == chunksize) {
            break;
        }

        contents += read_sz;
        rest_data_sz = chunksize - read_sz;
        resp->used_size = 0;
    }
    case VS_CLOUD_FETCH_Tl_STEP_KEYS:
        while (rest_data_sz && VS_CLOUD_FETCH_Tl_STEP_KEYS == resp->step) {
            size_t read_sz = rest_data_sz;
            size_t curr_key_sz = 0;

            if (resp->used_size < sizeof(vs_pubkey_dated_t)) {
                if (resp->used_size + rest_data_sz > sizeof(vs_pubkey_dated_t)) {
                    read_sz = sizeof(vs_pubkey_dated_t) - resp->used_size;
                }

            } else {
                vs_pubkey_dated_t *pubkey = (vs_pubkey_dated_t *)resp->buff;
                int key_len = vs_hsm_get_pubkey_len(pubkey->pubkey.ec_type);
                CHECK_RET(key_len > 0, 0, "Error fetch TL key. Unsupported ec type");

                curr_key_sz = sizeof(vs_pubkey_dated_t) + key_len + VS_IOT_NTOHS(pubkey->pubkey.meta_data_sz);
                if (resp->used_size + rest_data_sz > curr_key_sz) {
                    read_sz = curr_key_sz - resp->used_size;
                }
            }

            VS_IOT_MEMCPY(&(resp->buff[resp->used_size]), contents, read_sz);

            contents += read_sz;
            rest_data_sz -= read_sz;
            resp->used_size += read_sz;

            if (curr_key_sz != 0 && resp->used_size == curr_key_sz) {
                resp->used_size = 0;
                vs_tl_element_info_t info = {.id = VS_TL_ELEMENT_TLC, .index = 0};
                if (VS_CODE_OK != vs_tl_save_part(&info, (uint8_t *)resp->buff, curr_key_sz)) {
                    return 0;
                }

                if (++resp->keys_cnt == resp->host_header.pub_keys_count) {
                    resp->step = VS_CLOUD_FETCH_Tl_STEP_FOOTER;
                }
            }
        }

        if (!rest_data_sz) {
            break;
        }

    case VS_CLOUD_FETCH_Tl_STEP_FOOTER:
        while (rest_data_sz) {
            size_t read_sz = rest_data_sz;

            if (resp->used_size < sizeof(vs_tl_footer_t)) {
                if (resp->used_size + rest_data_sz > sizeof(vs_tl_footer_t)) {
                    read_sz = sizeof(vs_tl_footer_t) - resp->used_size;
                }
            } else if (0 != resp->footer_sz) {
                if (resp->used_size + rest_data_sz > resp->footer_sz) {
                    return 0;
                }
            } else {
                vs_tl_footer_t *footer_info = (vs_tl_footer_t *)resp->buff;

                if (resp->used_size < sizeof(vs_tl_footer_t) + sizeof(vs_sign_t) &&
                    resp->used_size + rest_data_sz >= sizeof(vs_tl_footer_t) + sizeof(vs_sign_t)) {
                    read_sz = sizeof(vs_tl_footer_t) + sizeof(vs_sign_t) - resp->used_size;
                } else {
                    vs_sign_t *element = (vs_sign_t *)footer_info->signatures;
                    int sign_len;
                    int key_len;

                    sign_len = vs_hsm_get_signature_len(element->ec_type);
                    key_len = vs_hsm_get_pubkey_len(element->ec_type);

                    CHECK_RET((key_len > 0 && sign_len > 0), 0, "[CLOUD] TL footer parse error");
                    resp->footer_sz = sizeof(vs_tl_footer_t) +
                                      resp->host_header.signatures_count * (sizeof(vs_sign_t) + key_len + sign_len);

                    if (resp->used_size + rest_data_sz > resp->footer_sz ||
                        resp->footer_sz > VS_TL_STORAGE_MAX_PART_SIZE) {
                        return 0;
                    }
                }
            }

            VS_IOT_MEMCPY(&(resp->buff[resp->used_size]), contents, read_sz);
            contents += read_sz;
            rest_data_sz -= read_sz;
            resp->used_size += read_sz;

            if (resp->footer_sz != 0 && resp->used_size == resp->footer_sz) {
                resp->used_size = 0;
                uint8_t type = ((vs_tl_footer_t *)resp->buff)->tl_type;
                vs_tl_element_info_t info = {.id = VS_TL_ELEMENT_TLF, .index = 0};

                if (VS_CODE_OK != vs_cloud_is_new_tl_version_available(type, &resp->host_header.version) ||
                    VS_CODE_OK != vs_tl_save_part(&info, (uint8_t *)resp->buff, resp->footer_sz)) {
                    return 0;
                }
                resp->step = VS_CLOUD_FETCH_Tl_STEP_DONE;
            }
        }
        break;
    default:
        return 0;
    }

    return chunksize;
}

/*************************************************************************/
vs_status_e
vs_cloud_fetch_and_store_tl(const char *tl_file_url) {
    vs_status_e res = VS_CODE_OK;
    CHECK_NOT_ZERO_RET(tl_file_url, VS_CODE_ERR_NULLPTR_ARGUMENT);
    CHECK_NOT_ZERO_RET(_hal_impl, VS_CODE_ERR_NOINIT);
    CHECK_NOT_ZERO_RET(_hal_impl->http_get, VS_CODE_ERR_NOINIT);
    size_t in_out_answer_len = 0;
    tl_resp_buff_t resp;
    VS_IOT_MEMSET(&resp, 0, sizeof(resp));

    resp.buff = VS_IOT_CALLOC(512, 1);

    if (VS_CODE_OK != _hal_impl->http_get(tl_file_url, NULL, _store_tl_handler, &resp, &in_out_answer_len) ||
        VS_CLOUD_FETCH_Tl_STEP_DONE != resp.step) {
        res = VS_CODE_ERR_CLOUD;
    }

    VS_IOT_FREE(resp.buff);
    return res;
}

/*************************************************************************/