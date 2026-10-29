/*
 * This is the source code of AyuGram for Desktop.
 *
 * We do not and cannot prevent the use of our code,
 * but be respectful and credit the original author.
 *
 * Copyright @Radolyn, 2026
 */
#include "ayu/secret/secret_chat_crypto.h"

#include "base/openssl_help.h"
#include "mtproto/mtproto_auth_key.h"
#include "mtproto/mtproto_dh_utils.h"

namespace Ayu::Secret {
namespace {

constexpr auto kFingerprintDataSize = 256;

struct AesKeyIv {
	bytes::vector key;
	bytes::vector iv;
};

[[nodiscard]] AesKeyIv DeriveAesKeyIv(
		bytes::const_span authKey,
		bytes::const_span msgKey,
		int x) {
	const auto sha256_a = openssl::Sha256(
		msgKey,
		authKey.subspan(x, 36));

	const auto sha256_b = openssl::Sha256(
		authKey.subspan(40 + x, 36),
		msgKey);

	auto aesKey = bytes::vector(32, gsl::byte{});
	bytes::copy(gsl::make_span(aesKey).subspan(0, 8), bytes::make_span(sha256_a).subspan(0, 8));
	bytes::copy(gsl::make_span(aesKey).subspan(8, 16), bytes::make_span(sha256_b).subspan(8, 16));
	bytes::copy(gsl::make_span(aesKey).subspan(24, 8), bytes::make_span(sha256_a).subspan(24, 8));

	auto aesIv = bytes::vector(32, gsl::byte{});
	bytes::copy(gsl::make_span(aesIv).subspan(0, 8), bytes::make_span(sha256_b).subspan(0, 8));
	bytes::copy(gsl::make_span(aesIv).subspan(8, 16), bytes::make_span(sha256_a).subspan(8, 16));
	bytes::copy(gsl::make_span(aesIv).subspan(24, 8), bytes::make_span(sha256_b).subspan(24, 8));

	return { std::move(aesKey), std::move(aesIv) };
}

} // namespace

bool Crypto::IsGoodDhConfig(int32 g, bytes::const_span primeBytes) {
	return MTP::IsPrimeAndGood(primeBytes, g);
}

KeyPair Crypto::GenerateKeyPair(
		int32 g,
		bytes::const_span primeBytes,
		bytes::const_span randomSeed) {
	auto modexp = MTP::CreateModExp(g, primeBytes, randomSeed);
	return KeyPair{
		.publicComponent = std::move(modexp.modexp),
		.privateKey = std::move(modexp.randomPower),
	};
}

bytes::vector Crypto::ComputeAuthKey(
		bytes::const_span remoteComponent,
		bytes::const_span localPrivateKey,
		bytes::const_span primeBytes) {
	return MTP::CreateAuthKey(remoteComponent, localPrivateKey, primeBytes);
}

uint64 Crypto::ComputeFingerprint(bytes::const_span authKey) {
	if (authKey.size() != kFingerprintDataSize) {
		return 0;
	}
	const auto hash = openssl::Sha1(authKey);
	return (gsl::to_integer<uint64>(hash[19]) << 56)
		| (gsl::to_integer<uint64>(hash[18]) << 48)
		| (gsl::to_integer<uint64>(hash[17]) << 40)
		| (gsl::to_integer<uint64>(hash[16]) << 32)
		| (gsl::to_integer<uint64>(hash[15]) << 24)
		| (gsl::to_integer<uint64>(hash[14]) << 16)
		| (gsl::to_integer<uint64>(hash[13]) << 8)
		| (gsl::to_integer<uint64>(hash[12]));
}

bytes::vector Crypto::ComputeVisualFingerprint(bytes::const_span authKey) {
	const auto hash = openssl::Sha256(authKey);
	return bytes::make_vector(hash);
}

bytes::vector Crypto::EncryptPacket(
		uint64 keyFingerprint,
		bytes::const_span authKey,
		bytes::const_span plaintext,
		bool isCreator) {
	if (authKey.size() != 256 || plaintext.empty()) {
		return {};
	}
	const auto dataLen = uint32(plaintext.size());
	constexpr auto kMinPadding = 16;
	const auto unpaddedLen = 4 + dataLen;
	const auto padding = kMinPadding + (16 - ((unpaddedLen + kMinPadding) % 16)) % 16;
	const auto totalPaddedLen = unpaddedLen + padding;

	auto padded = bytes::vector(totalPaddedLen, gsl::byte{});
	padded[0] = gsl::byte(dataLen & 0xFF);
	padded[1] = gsl::byte((dataLen >> 8) & 0xFF);
	padded[2] = gsl::byte((dataLen >> 16) & 0xFF);
	padded[3] = gsl::byte((dataLen >> 24) & 0xFF);

	bytes::copy(gsl::make_span(padded).subspan(4, dataLen), plaintext);
	bytes::set_random(gsl::make_span(padded).subspan(unpaddedLen, padding));

	const auto x = isCreator ? 0 : 8;
	const auto msgKeyLarge = openssl::Sha256(
		authKey.subspan(88 + x, 32),
		bytes::make_span(padded));
	const auto msgKey = bytes::make_vector(bytes::make_span(msgKeyLarge).subspan(8, 16));

	const auto [aesKey, aesIv] = DeriveAesKeyIv(authKey, msgKey, x);

	auto encrypted = bytes::vector(totalPaddedLen, gsl::byte{});
	aesIgeEncryptRaw(
		padded.data(),
		encrypted.data(),
		totalPaddedLen,
		aesKey.data(),
		aesIv.data());

	auto packet = bytes::vector(8 + 16 + totalPaddedLen, gsl::byte{});
	for (auto i = 0; i < 8; ++i) {
		packet[i] = gsl::byte((keyFingerprint >> (i * 8)) & 0xFF);
	}
	bytes::copy(gsl::make_span(packet).subspan(8, 16), msgKey);
	bytes::copy(gsl::make_span(packet).subspan(24), encrypted);

	return packet;
}

std::optional<DecryptedPacket> Crypto::DecryptPacket(
		bytes::const_span authKey,
		bytes::const_span packet,
		bool isCreator) {
	if (authKey.size() != 256 || packet.size() < (8 + 16 + 16)) {
		return std::nullopt;
	}
	uint64 keyFingerprint = 0;
	for (auto i = 0; i < 8; ++i) {
		keyFingerprint |= (gsl::to_integer<uint64>(packet[i]) << (i * 8));
	}

	const auto msgKey = packet.subspan(8, 16);
	const auto encrypted = packet.subspan(24);
	if (encrypted.size() % 16 != 0) {
		return std::nullopt;
	}

	const auto x = isCreator ? 8 : 0;
	const auto [aesKey, aesIv] = DeriveAesKeyIv(authKey, msgKey, x);

	auto decrypted = bytes::vector(encrypted.size(), gsl::byte{});
	aesIgeDecryptRaw(
		encrypted.data(),
		decrypted.data(),
		encrypted.size(),
		aesKey.data(),
		aesIv.data());

	const auto msgKeyLarge = openssl::Sha256(
		authKey.subspan(88 + x, 32),
		bytes::make_span(decrypted));
	if (bytes::compare(bytes::make_span(msgKeyLarge).subspan(8, 16), msgKey) != 0) {
		return std::nullopt;
	}

	const auto dataLen = gsl::to_integer<uint32>(decrypted[0])
		| (gsl::to_integer<uint32>(decrypted[1]) << 8)
		| (gsl::to_integer<uint32>(decrypted[2]) << 16)
		| (gsl::to_integer<uint32>(decrypted[3]) << 24);

	if (dataLen > decrypted.size() - 4) {
		return std::nullopt;
	}

	auto plaintext = bytes::make_vector(
		bytes::make_span(decrypted).subspan(4, dataLen));

	return DecryptedPacket{
		.keyFingerprint = keyFingerprint,
		.plaintext = std::move(plaintext),
	};
}

} // namespace Ayu::Secret
