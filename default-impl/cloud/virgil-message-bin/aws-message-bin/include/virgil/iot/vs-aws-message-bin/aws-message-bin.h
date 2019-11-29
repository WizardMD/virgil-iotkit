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

/**
 * @file aws-message-bin.h
 * @brief Default transport for gateway communication.
 *
 * MQTT used as transport.
 * Example of usage inside #vs_cloud_init function:
 *  \code

vs_storage_op_ctx_t slots_storage_impl;     // Storage implementation for slot
vs_secmodule_impl_t *secmodule_impl;        // Security implementation

// Init storage implementation
vs_app_storage_init_impl(&slots_storage_impl, vs_app_slots_dir(), VS_SLOTS_STORAGE_MAX_SIZE)

// You can initialize security module by software implementation :
secmodule_impl = vs_soft_secmodule_impl(&slots_storage_impl);

// Cloud initialization
vs_cloud_init(vs_curl_http_impl(), vs_aws_message_bin_impl(), secmodule_impl);
\endcode
*
* You need to implement custom storage. As an example you can see default implementation in
* <a
href="https://github.com/VirgilSecurity/demo-iotkit-nix/blob/release/v0.1.0-alpha/common/src/helpers/app-storage.c#L73">vs_app_storage_init_impl()</a>
function in app-storage.c file.
*/

#ifndef VS_AWS_DEFAULT_MESSAGE_BIN_IMPL_H
#define VS_AWS_DEFAULT_MESSAGE_BIN_IMPL_H

#include <virgil/iot/cloud/cloud.h>

/**  Returns MQTT based implementation of transport.
 *
 * \return #vs_cloud_message_bin_impl_t
 */
const vs_cloud_message_bin_impl_t *
vs_aws_message_bin_impl(void);

#endif // VS_AWS_DEFAULT_MESSAGE_BIN_IMPL_H
