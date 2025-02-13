// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/quartc/quartc_crypto_helpers.h"
#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/core/tls_client_handshaker.h"
#include "net/third_party/quic/core/tls_server_handshaker.h"

namespace quic {

void DummyProofSource::GetProof(const QuicSocketAddress& server_address,
                                const QuicString& hostname,
                                const QuicString& server_config,
                                QuicTransportVersion transport_version,
                                QuicStringPiece chlo_hash,
                                std::unique_ptr<Callback> callback) {
  QuicReferenceCountedPointer<ProofSource::Chain> chain =
      GetCertChain(server_address, hostname);
  QuicCryptoProof proof;
  proof.signature = "Dummy signature";
  proof.leaf_cert_scts = "Dummy timestamp";
  callback->Run(true, chain, proof, nullptr /* details */);
}

QuicReferenceCountedPointer<DummyProofSource::Chain>
DummyProofSource::GetCertChain(const QuicSocketAddress& server_address,
                               const QuicString& hostname) {
  std::vector<QuicString> certs;
  certs.push_back("Dummy cert");
  return QuicReferenceCountedPointer<ProofSource::Chain>(
      new ProofSource::Chain(certs));
}

void DummyProofSource::ComputeTlsSignature(
    const QuicSocketAddress& server_address,
    const QuicString& hostname,
    uint16_t signature_algorithm,
    QuicStringPiece in,
    std::unique_ptr<SignatureCallback> callback) {
  callback->Run(true, "Dummy signature");
}

QuicAsyncStatus InsecureProofVerifier::VerifyProof(
    const QuicString& hostname,
    const uint16_t port,
    const QuicString& server_config,
    QuicTransportVersion transport_version,
    QuicStringPiece chlo_hash,
    const std::vector<QuicString>& certs,
    const QuicString& cert_sct,
    const QuicString& signature,
    const ProofVerifyContext* context,
    QuicString* error_details,
    std::unique_ptr<ProofVerifyDetails>* verify_details,
    std::unique_ptr<ProofVerifierCallback> callback) {
  return QUIC_SUCCESS;
}

QuicAsyncStatus InsecureProofVerifier::VerifyCertChain(
    const QuicString& hostname,
    const std::vector<QuicString>& certs,
    const ProofVerifyContext* context,
    QuicString* error_details,
    std::unique_ptr<ProofVerifyDetails>* details,
    std::unique_ptr<ProofVerifierCallback> callback) {
  return QUIC_SUCCESS;
}

std::unique_ptr<ProofVerifyContext>
InsecureProofVerifier::CreateDefaultContext() {
  return nullptr;
}

QuicConnectionId QuartcCryptoServerStreamHelper::GenerateConnectionIdForReject(
    QuicTransportVersion version,
    QuicConnectionId connection_id) const {
  // TODO(b/124399417):  Request a zero-length connection id here when the QUIC
  // server perspective supports it.  Right now, the stateless rejector requires
  // a connection id that is not the same as the client-chosen connection id.
  return QuicUtils::CreateRandomConnectionId();
}

bool QuartcCryptoServerStreamHelper::CanAcceptClientHello(
    const CryptoHandshakeMessage& message,
    const QuicSocketAddress& client_address,
    const QuicSocketAddress& peer_address,
    const QuicSocketAddress& self_address,
    QuicString* error_details) const {
  return true;
}

std::unique_ptr<QuicCryptoClientConfig> CreateCryptoClientConfig(
    QuicStringPiece pre_shared_key) {
  auto config = QuicMakeUnique<QuicCryptoClientConfig>(
      QuicMakeUnique<InsecureProofVerifier>(),
      TlsClientHandshaker::CreateSslCtx());
  config->set_pad_inchoate_hello(false);
  config->set_pad_full_hello(false);
  if (!pre_shared_key.empty()) {
    config->set_pre_shared_key(pre_shared_key);
  }
  return config;
}

std::unique_ptr<QuicCryptoServerConfig> CreateCryptoServerConfig(
    QuicRandom* random,
    const QuicClock* clock,
    QuicStringPiece pre_shared_key) {
  // Generate a random source address token secret. For long-running servers
  // it's better to not regenerate it for each connection to enable zero-RTT
  // handshakes, but for transient clients it does not matter.
  char source_address_token_secret[kInputKeyingMaterialLength];
  random->RandBytes(source_address_token_secret, kInputKeyingMaterialLength);
  auto config = QuicMakeUnique<QuicCryptoServerConfig>(
      QuicString(source_address_token_secret, kInputKeyingMaterialLength),
      random, QuicMakeUnique<DummyProofSource>(), KeyExchangeSource::Default(),
      TlsServerHandshaker::CreateSslCtx());

  // We run QUIC over ICE, and ICE is verifying remote side with STUN pings.
  // We disable source address token validation in order to allow for 0-rtt
  // setup (plus source ip addresses are changing even during the connection
  // when ICE is used).
  config->set_validate_source_address_token(false);

  // Effectively disables the anti-amplification measures (we don't need
  // them because we use ICE, and we need to disable them because we disable
  // padding of crypto packets).
  // This multiplier must be large enough so that the crypto handshake packet
  // (approx. 300 bytes) multiplied by this multiplier is larger than a fully
  // sized packet (currently 1200 bytes).
  // 1500 is a bit extreme: if you can imagine sending a 1 byte packet, and
  // your largest MTU would be below 1500 bytes, 1500*1 >=
  // any_packet_that_you_can_imagine_sending.
  // (again, we hardcode packet size to 1200, so we are not dealing with jumbo
  // frames).
  config->set_chlo_multiplier(1500);

  // We are sending small client hello, we must not validate its size.
  config->set_validate_chlo_size(false);

  // Provide server with serialized config string to prove ownership.
  QuicCryptoServerConfig::ConfigOptions options;
  // The |message| is used to handle the return value of AddDefaultConfig
  // which is raw pointer of the CryptoHandshakeMessage.
  std::unique_ptr<CryptoHandshakeMessage> message(
      config->AddDefaultConfig(random, clock, options));
  config->set_pad_rej(false);
  config->set_pad_shlo(false);
  if (!pre_shared_key.empty()) {
    config->set_pre_shared_key(pre_shared_key);
  }
  return config;
}

}  // namespace quic
