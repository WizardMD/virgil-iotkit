/**
 * Copyright (C) 2016 Virgil Security Inc.
 *
 * Lead Maintainer: Virgil Security Inc. <support@virgilsecurity.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     (1) Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *     (2) Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 *     (3) Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef VIRGIL_SORAA_MANUFACTURE_VIRGILCRYPTOSIGNER_H
#define VIRGIL_SORAA_MANUFACTURE_VIRGILCRYPTOSIGNER_H

#include <memory>

#include <virgil/soraa/initializer/SignerInterface.h>
#include <virgil/sdk/crypto/keys/PrivateKey.h>

namespace virgil {
namespace soraa {
    namespace initializer {
        class VirgilCryptoSigner: public SignerInterface {
        public:
            VirgilCryptoSigner(std::shared_ptr<sdk::crypto::Crypto> crypto,
                               sdk::crypto::keys::PrivateKey privateKey);

            VirgilByteArray sign(const VirgilByteArray &data) override;
            bool verify(const VirgilByteArray &data, const VirgilByteArray &signature, const VirgilByteArray &publicKey) override;
            uint16_t signerId() override;
            VirgilByteArray publicKeyFull() override;

        private:
            std::shared_ptr<sdk::crypto::Crypto> crypto_;
            sdk::crypto::keys::PrivateKey privateKey_;
        };
    }
}
}

#endif //VIRGIL_SORAA_MANUFACTURE_VIRGILCRYPTOSIGNER_H