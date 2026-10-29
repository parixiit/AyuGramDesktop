/*
 * This is the source code of AyuGram for Desktop.
 *
 * We do not and cannot prevent the use of our code,
 * but be respectful and credit the original author.
 *
 * Copyright @Radolyn, 2026
 */
#pragma once

#include "base/bytes.h"

#include <optional>
#include <vector>

namespace Ayu::Secret {

struct DhConfig {
	int32 g = 0;
	bytes::vector p;
	int32 version = 0;
};

struct KeyPair {
	bytes::vector publicComponent; // g_a or g_b (256 bytes)
	bytes::vector privateKey;      // a or b (256 bytes)
};

struct DecryptedPacket {
	uint64 keyFingerprint = 0;
	bytes::vector plaintext;
};

class Crypto final {
public:
	// Verify prime p and generator g
	[[nodiscard]] static bool IsGoodDhConfig(int32 g, bytes::const_span primeBytes);

	// Generate private power (a or b) and public component (g_a or g_b)
	[[nodiscard]] static KeyPair GenerateKeyPair(
		int32 g,
		bytes::const_span primeBytes,
		bytes::const_span randomSeed = {});

	// Compute shared auth key K = (remote_component)^(local_private_key) mod p
	[[nodiscard]] static bytes::vector ComputeAuthKey(
		bytes::const_span remoteComponent,
		bytes::const_span localPrivateKey,
		bytes::const_span primeBytes);

	// Compute 64-bit key fingerprint (lower 64 bits of SHA1(auth_key))
	[[nodiscard]] static uint64 ComputeFingerprint(bytes::const_span authKey);

	// Compute 16-byte visual fingerprint (SHA256(auth_key))
	[[nodiscard]] static bytes::vector ComputeVisualFingerprint(bytes::const_span authKey);

	// MTProto 2.0 Encrypt: packs length, padding, derives keys, encrypts via AES-IGE
	[[nodiscard]] static bytes::vector EncryptPacket(
		uint64 keyFingerprint,
		bytes::const_span authKey,
		bytes::const_span plaintext,
		bool isCreator);

	// MTProto 2.0 Decrypt: parses header, verifies msg_key, decrypts via AES-IGE, strips padding
	[[nodiscard]] static std::optional<DecryptedPacket> DecryptPacket(
		bytes::const_span authKey,
		bytes::const_span packet,
		bool isCreator);

};

} // namespace Ayu::Secret
