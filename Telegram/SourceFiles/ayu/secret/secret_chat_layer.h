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

#include <QtCore/QString>
#include <optional>
#include <vector>

namespace Ayu::Secret {

inline constexpr uint32 kDecryptedMessageLayerId = 0x1be31789;
inline constexpr uint32 kDecryptedMessageId = 0x91cc4674;
inline constexpr uint32 kDecryptedMessageServiceId = 0x73164160;

inline constexpr uint32 kActionSetMessageTTLId = 0xa1733aec;
inline constexpr uint32 kActionReadMessagesId = 0x0c488514;
inline constexpr uint32 kActionDeleteMessagesId = 0x65614304;
inline constexpr uint32 kActionScreenshotMessagesId = 0x8ac1f475;
inline constexpr uint32 kActionFlushHistoryId = 0x6719e45c;
inline constexpr uint32 kActionResendId = 0x51111913;
inline constexpr uint32 kActionNotifyLayerId = 0xf3048883;
inline constexpr uint32 kActionTypingId = 0xccb27641;

inline constexpr uint32 kDecryptedMessageMediaEmptyId = 0x089f5c46;
inline constexpr uint32 kDecryptedMessageMediaPhotoId = 0xf14d96a4;
inline constexpr uint32 kDecryptedMessageMediaVideoId = 0x970c8c0e;
inline constexpr uint32 kDecryptedMessageMediaDocumentId = 0x7afe8ac2;
inline constexpr uint32 kDecryptedMessageMediaAudioId = 0x57e0a9cb;

enum class MediaType {
	Empty,
	Photo,
	Video,
	Document,
	Audio,
};

struct DecryptedMedia {
	MediaType type = MediaType::Empty;
	bytes::vector key;
	bytes::vector iv;
	bytes::vector thumb;
	int32 thumbW = 0;
	int32 thumbH = 0;
	int32 w = 0;
	int32 h = 0;
	int32 size = 0;
	int32 duration = 0;
	QString mimeType;
	QString fileName;
	QString caption;
};

struct DecryptedMessage {
	uint64 randomId = 0;
	int32 ttl = 0;
	QString message;
	bool silent = false;
	bool noWebpage = false;
	uint64 replyToRandomId = 0;
	std::optional<DecryptedMedia> media;
};

struct DecryptedServiceMessage {
	uint64 randomId = 0;
	uint32 actionConstructor = 0;
	int32 ttlSeconds = 0;
	int32 layer = 0;
	std::vector<uint64> affectedRandomIds;
};

struct DecryptedLayer {
	int32 layer = 73;
	int32 inSeqNo = 0;
	int32 outSeqNo = 0;
	std::optional<DecryptedMessage> message;
	std::optional<DecryptedServiceMessage> serviceMessage;
};

class LayerCodec final {
public:
	[[nodiscard]] static bytes::vector SerializeLayer(
		int32 layer,
		int32 inSeqNo,
		int32 outSeqNo,
		const DecryptedMessage &message);

	[[nodiscard]] static bytes::vector SerializeServiceLayer(
		int32 layer,
		int32 inSeqNo,
		int32 outSeqNo,
		const DecryptedServiceMessage &service);

	[[nodiscard]] static std::optional<DecryptedLayer> DeserializeLayer(
		bytes::const_span data);

};

} // namespace Ayu::Secret
