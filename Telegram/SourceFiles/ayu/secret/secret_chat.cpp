/*
 * This is the source code of AyuGram for Desktop.
 *
 * We do not and cannot prevent the use of our code,
 * but be respectful and credit the original author.
 *
 * Copyright @Radolyn, 2026
 */
#include "ayu/secret/secret_chat.h"

namespace Ayu::Secret {

SecretChat::SecretChat(
	int32 chatId,
	uint64 accessHash,
	UserId partnerId,
	bool isCreator)
: _chatId(chatId)
, _accessHash(accessHash)
, _partnerId(partnerId)
, _isCreator(isCreator)
, _createdAt(crl::now()) {
}

int32 SecretChat::chatId() const {
	return _chatId;
}

uint64 SecretChat::accessHash() const {
	return _accessHash;
}

UserId SecretChat::partnerId() const {
	return _partnerId;
}

bool SecretChat::isCreator() const {
	return _isCreator;
}

State SecretChat::state() const {
	return _state;
}

void SecretChat::setState(State state) {
	_state = state;
}

bytes::const_span SecretChat::authKey() const {
	return _authKey;
}

void SecretChat::setAuthKey(bytes::vector key) {
	_authKey = std::move(key);
}

uint64 SecretChat::keyFingerprint() const {
	return _keyFingerprint;
}

void SecretChat::setKeyFingerprint(uint64 fingerprint) {
	_keyFingerprint = fingerprint;
}

bytes::const_span SecretChat::localPrivateKey() const {
	return _localPrivateKey;
}

void SecretChat::setLocalPrivateKey(bytes::vector key) {
	_localPrivateKey = std::move(key);
}

int32 SecretChat::inSeqNo() const {
	return _inSeqNo;
}

int32 SecretChat::outSeqNo() const {
	return _outSeqNo;
}

void SecretChat::advanceInSeqNo() {
	_inSeqNo += 2;
}

void SecretChat::advanceOutSeqNo() {
	_outSeqNo += 2;
}

void SecretChat::setSeqNos(int32 inSeq, int32 outSeq) {
	_inSeqNo = inSeq;
	_outSeqNo = outSeq;
}

int32 SecretChat::layer() const {
	return _layer;
}

void SecretChat::setLayer(int32 layer) {
	_layer = layer;
}

int32 SecretChat::ttl() const {
	return _ttl;
}

void SecretChat::setTtl(int32 ttl) {
	_ttl = ttl;
}

crl::time SecretChat::createdAt() const {
	return _createdAt;
}

} // namespace Ayu::Secret
