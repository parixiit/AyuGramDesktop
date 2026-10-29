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
#include "data/data_peer_id.h"

namespace Ayu::Secret {

enum class State {
	Waiting,
	Requested,
	Ready,
	Discarded,
};

class SecretChat final {
public:
	SecretChat(
		int32 chatId,
		uint64 accessHash,
		UserId partnerId,
		bool isCreator);

	[[nodiscard]] int32 chatId() const;
	[[nodiscard]] uint64 accessHash() const;
	[[nodiscard]] UserId partnerId() const;
	[[nodiscard]] bool isCreator() const;

	[[nodiscard]] State state() const;
	void setState(State state);

	[[nodiscard]] bytes::const_span authKey() const;
	void setAuthKey(bytes::vector key);

	[[nodiscard]] uint64 keyFingerprint() const;
	void setKeyFingerprint(uint64 fingerprint);

	[[nodiscard]] bytes::const_span localPrivateKey() const;
	void setLocalPrivateKey(bytes::vector key);

	[[nodiscard]] int32 inSeqNo() const;
	[[nodiscard]] int32 outSeqNo() const;
	void advanceInSeqNo();
	void advanceOutSeqNo();
	void setSeqNos(int32 inSeq, int32 outSeq);

	[[nodiscard]] int32 layer() const;
	void setLayer(int32 layer);

	[[nodiscard]] int32 ttl() const;
	void setTtl(int32 ttl);

	[[nodiscard]] crl::time createdAt() const;

private:
	int32 _chatId = 0;
	uint64 _accessHash = 0;
	UserId _partnerId;
	bool _isCreator = false;
	State _state = State::Waiting;

	bytes::vector _localPrivateKey;
	bytes::vector _authKey;
	uint64 _keyFingerprint = 0;

	int32 _inSeqNo = 0;
	int32 _outSeqNo = 0;
	int32 _layer = 73;
	int32 _ttl = 0;
	crl::time _createdAt = 0;

};

} // namespace Ayu::Secret
