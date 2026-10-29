/*
 * This is the source code of AyuGram for Desktop.
 *
 * We do not and cannot prevent the use of our code,
 * but be respectful and credit the original author.
 *
 * Copyright @Radolyn, 2026
 */
#include "ayu/secret/secret_chat_layer.h"

#include "base/random.h"

namespace Ayu::Secret {
namespace {

class TLWriter final {
public:
	void writeUInt32(uint32 v) {
		_buf.push_back(gsl::byte(v & 0xFF));
		_buf.push_back(gsl::byte((v >> 8) & 0xFF));
		_buf.push_back(gsl::byte((v >> 16) & 0xFF));
		_buf.push_back(gsl::byte((v >> 24) & 0xFF));
	}

	void writeInt32(int32 v) {
		writeUInt32(uint32(v));
	}

	void writeUInt64(uint64 v) {
		writeUInt32(uint32(v & 0xFFFFFFFF));
		writeUInt32(uint32((v >> 32) & 0xFFFFFFFF));
	}

	void writeInt64(int64 v) {
		writeUInt64(uint64(v));
	}

	void writeBytes(bytes::const_span b) {
		const auto len = b.size();
		int padding = 0;
		if (len < 254) {
			_buf.push_back(gsl::byte(len));
			padding = (4 - (1 + len) % 4) % 4;
		} else {
			_buf.push_back(gsl::byte(0xFE));
			_buf.push_back(gsl::byte(len & 0xFF));
			_buf.push_back(gsl::byte((len >> 8) & 0xFF));
			_buf.push_back(gsl::byte((len >> 16) & 0xFF));
			padding = (4 - len % 4) % 4;
		}
		const auto oldSize = _buf.size();
		_buf.resize(oldSize + len + padding);
		bytes::copy(gsl::make_span(_buf).subspan(oldSize, len), b);
		for (auto i = 0; i < padding; ++i) {
			_buf[oldSize + len + i] = gsl::byte(0);
		}
	}

	void writeString(const QString &s) {
		const auto utf8 = s.toUtf8();
		writeBytes(bytes::make_span(reinterpret_cast<const gsl::byte*>(utf8.constData()), utf8.size()));
	}

	[[nodiscard]] bytes::vector result() && {
		return std::move(_buf);
	}

private:
	bytes::vector _buf;

};

class TLReader final {
public:
	explicit TLReader(bytes::const_span data) : _data(data) {}

	[[nodiscard]] bool hasMore(size_t count) const {
		return _pos + count <= _data.size();
	}

	bool readUInt32(uint32 &out) {
		if (!hasMore(4)) return false;
		out = gsl::to_integer<uint32>(_data[_pos])
			| (gsl::to_integer<uint32>(_data[_pos + 1]) << 8)
			| (gsl::to_integer<uint32>(_data[_pos + 2]) << 16)
			| (gsl::to_integer<uint32>(_data[_pos + 3]) << 24);
		_pos += 4;
		return true;
	}

	bool readInt32(int32 &out) {
		uint32 v = 0;
		if (!readUInt32(v)) return false;
		out = int32(v);
		return true;
	}

	bool readUInt64(uint64 &out) {
		uint32 low = 0, high = 0;
		if (!readUInt32(low) || !readUInt32(high)) return false;
		out = uint64(low) | (uint64(high) << 32);
		return true;
	}

	bool readInt64(int64 &out) {
		uint64 v = 0;
		if (!readUInt64(v)) return false;
		out = int64(v);
		return true;
	}

	bool readBytes(bytes::vector &out) {
		if (!hasMore(1)) return false;
		size_t len = 0;
		int padding = 0;
		const auto first = gsl::to_integer<uint32>(_data[_pos++]);
		if (first < 254) {
			len = first;
			padding = (4 - (1 + len) % 4) % 4;
		} else {
			if (!hasMore(3)) return false;
			len = gsl::to_integer<uint32>(_data[_pos])
				| (gsl::to_integer<uint32>(_data[_pos + 1]) << 8)
				| (gsl::to_integer<uint32>(_data[_pos + 2]) << 16);
			_pos += 3;
			padding = (4 - len % 4) % 4;
		}
		if (!hasMore(len + padding)) return false;
		out = bytes::make_vector(_data.subspan(_pos, len));
		_pos += len + padding;
		return true;
	}

	bool readString(QString &out) {
		bytes::vector b;
		if (!readBytes(b)) return false;
		out = QString::fromUtf8(reinterpret_cast<const char*>(b.data()), b.size());
		return true;
	}

private:
	bytes::const_span _data;
	size_t _pos = 0;

};

} // namespace

bytes::vector LayerCodec::SerializeLayer(
		int32 layer,
		int32 inSeqNo,
		int32 outSeqNo,
		const DecryptedMessage &message) {
	TLWriter w;
	w.writeUInt32(kDecryptedMessageLayerId);

	// random_bytes (16 bytes)
	auto randomBytes = bytes::vector(16, gsl::byte{});
	bytes::set_random(randomBytes);
	w.writeBytes(randomBytes);

	w.writeInt32(layer);
	w.writeInt32(inSeqNo);
	w.writeInt32(outSeqNo);

	// Inner message: decryptedMessage#91cc4674
	w.writeUInt32(kDecryptedMessageId);
	uint32 flags = 0;
	if (message.noWebpage) flags |= (1 << 1);
	if (message.replyToRandomId != 0) flags |= (1 << 3);
	if (message.silent) flags |= (1 << 5);
	w.writeUInt32(flags);
	w.writeUInt64(message.randomId);
	w.writeInt32(message.ttl);
	w.writeString(message.message);

	if (message.replyToRandomId != 0) {
		w.writeUInt64(message.replyToRandomId);
	}

	return std::move(w).result();
}

bytes::vector LayerCodec::SerializeServiceLayer(
		int32 layer,
		int32 inSeqNo,
		int32 outSeqNo,
		const DecryptedServiceMessage &service) {
	TLWriter w;
	w.writeUInt32(kDecryptedMessageLayerId);

	auto randomBytes = bytes::vector(16, gsl::byte{});
	bytes::set_random(randomBytes);
	w.writeBytes(randomBytes);

	w.writeInt32(layer);
	w.writeInt32(inSeqNo);
	w.writeInt32(outSeqNo);

	// Inner: decryptedMessageService#73164160
	w.writeUInt32(kDecryptedMessageServiceId);
	w.writeUInt64(service.randomId);
	w.writeUInt32(service.actionConstructor);

	if (service.actionConstructor == kActionSetMessageTTLId) {
		w.writeInt32(service.ttlSeconds);
	} else if (service.actionConstructor == kActionNotifyLayerId) {
		w.writeInt32(service.layer);
	} else if (service.actionConstructor == kActionReadMessagesId
			|| service.actionConstructor == kActionDeleteMessagesId
			|| service.actionConstructor == kActionScreenshotMessagesId) {
		// Vector<long>: 0x1cb5c415 (vector constructor) + count + elements
		w.writeUInt32(0x1cb5c415);
		w.writeInt32(int32(service.affectedRandomIds.size()));
		for (const auto id : service.affectedRandomIds) {
			w.writeUInt64(id);
		}
	}

	return std::move(w).result();
}

std::optional<DecryptedLayer> LayerCodec::DeserializeLayer(bytes::const_span data) {
	TLReader r(data);
	uint32 layerConstructor = 0;
	if (!r.readUInt32(layerConstructor)) {
		return std::nullopt;
	}

	DecryptedLayer result;
	if (layerConstructor == kDecryptedMessageLayerId) {
		bytes::vector randomBytes;
		if (!r.readBytes(randomBytes)) return std::nullopt;
		if (!r.readInt32(result.layer)) return std::nullopt;
		if (!r.readInt32(result.inSeqNo)) return std::nullopt;
		if (!r.readInt32(result.outSeqNo)) return std::nullopt;
		if (!r.readUInt32(layerConstructor)) return std::nullopt;
	}

	if (layerConstructor == kDecryptedMessageId) {
		DecryptedMessage m;
		uint32 flags = 0;
		if (!r.readUInt32(flags)) return std::nullopt;
		m.noWebpage = (flags & (1 << 1)) != 0;
		m.silent = (flags & (1 << 5)) != 0;
		if (!r.readUInt64(m.randomId)) return std::nullopt;
		if (!r.readInt32(m.ttl)) return std::nullopt;
		if (!r.readString(m.message)) return std::nullopt;
		if (flags & (1 << 3)) {
			if (!r.readUInt64(m.replyToRandomId)) return std::nullopt;
		}
		result.message = std::move(m);
		return result;
	} else if (layerConstructor == kDecryptedMessageServiceId) {
		DecryptedServiceMessage s;
		if (!r.readUInt64(s.randomId)) return std::nullopt;
		if (!r.readUInt32(s.actionConstructor)) return std::nullopt;
		if (s.actionConstructor == kActionSetMessageTTLId) {
			if (!r.readInt32(s.ttlSeconds)) return std::nullopt;
		} else if (s.actionConstructor == kActionNotifyLayerId) {
			if (!r.readInt32(s.layer)) return std::nullopt;
		} else if (s.actionConstructor == kActionReadMessagesId
				|| s.actionConstructor == kActionDeleteMessagesId
				|| s.actionConstructor == kActionScreenshotMessagesId) {
			uint32 vectorCtor = 0;
			int32 count = 0;
			if (r.readUInt32(vectorCtor) && r.readInt32(count) && count > 0 && count < 1000) {
				s.affectedRandomIds.reserve(count);
				for (auto i = 0; i < count; ++i) {
					uint64 id = 0;
					if (r.readUInt64(id)) {
						s.affectedRandomIds.push_back(id);
					}
				}
			}
		}
		result.serviceMessage = std::move(s);
		return result;
	}

	return std::nullopt;
}

} // namespace Ayu::Secret
