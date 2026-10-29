/*
 * This is the source code of AyuGram for Desktop.
 *
 * We do not and cannot prevent the use of our code,
 * but be respectful and credit the original author.
 *
 * Copyright @Radolyn, 2026
 */
#include "ayu/secret/secret_chat_manager.h"

#include "apiwrap.h"
#include "base/random.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "mtproto/mtproto_dh_utils.h"

namespace Ayu::Secret {

Manager::Manager(not_null<Main::Session*> session)
: _session(session) {
}

Manager::~Manager() = default;

std::shared_ptr<SecretChat> Manager::chat(int32 chatId) const {
	const auto it = _chats.find(chatId);
	return (it != _chats.end()) ? it->second : nullptr;
}

std::shared_ptr<SecretChat> Manager::chatForUser(UserId userId) const {
	for (const auto &[id, chat] : _chats) {
		if (chat->partnerId() == userId && chat->state() == State::Ready) {
			return chat;
		}
	}
	return nullptr;
}

const std::map<int32, std::shared_ptr<SecretChat>> &Manager::chats() const {
	return _chats;
}

rpl::producer<not_null<SecretChat*>> Manager::chatUpdated() const {
	return _chatUpdated.events();
}

rpl::producer<IncomingSecretMessage> Manager::messageReceived() const {
	return _messageReceived.events();
}

void Manager::ensureDhConfig(Fn<void(const DhConfig &)> callback) {
	if (_dhConfig.has_value() && !_dhConfig->p.empty()) {
		callback(*_dhConfig);
		return;
	}

	const auto version = _dhConfig ? _dhConfig->version : 0;
	_session->api().request(MTPmessages_GetDhConfig(
		MTP_int(version),
		MTP_int(MTP::ModExpFirst::kRandomPowerSize)
	)).done([=](const MTPmessages_DhConfig &result) {
		result.match([&](const MTPDmessages_dhConfig &data) {
			auto prime = bytes::make_vector(data.vp().v);
			if (Crypto::IsGoodDhConfig(data.vg().v, prime)) {
				_dhConfig = DhConfig{
					.g = data.vg().v,
					.p = std::move(prime),
					.version = data.vversion().v,
				};
				callback(*_dhConfig);
			}
		}, [&](const MTPDmessages_dhConfigNotModified &data) {
			if (_dhConfig.has_value()) {
				callback(*_dhConfig);
			}
		});
	}).send();
}

void Manager::requestChat(not_null<UserData*> user) {
	ensureDhConfig([=](const DhConfig &dh) {
		auto keyPair = Crypto::GenerateKeyPair(dh.g, dh.p);
		const auto randomId = base::RandomValue<int32>();

		_session->api().request(MTPmessages_RequestEncryption(
			user->inputUser(),
			MTP_int(randomId),
			MTP_bytes(keyPair.publicComponent)
		)).done([=, priv = std::move(keyPair.privateKey)](const MTPEncryptedChat &result) {
			result.match([&](const MTPDencryptedChatWaiting &data) {
				const auto chatId = data.vid().v;
				const auto accessHash = uint64(data.vaccess_hash().v);
				auto chat = std::make_shared<SecretChat>(
					chatId,
					accessHash,
					user->id,
					true);
				chat->setLocalPrivateKey(std::move(priv));
				chat->setState(State::Waiting);
				_chats[chatId] = chat;
				_chatUpdated.fire(chat.get());
			}, [](const auto &) {
			});
		}).send();
	});
}

void Manager::acceptChat(int32 chatId) {
	const auto secret = chat(chatId);
	if (!secret || secret->state() != State::Requested) {
		return;
	}
	ensureDhConfig([=](const DhConfig &dh) {
		finishAccept(secret, secret->authKey(), dh);
	});
}

void Manager::finishAccept(
		const std::shared_ptr<SecretChat> &chat,
		bytes::const_span remoteG_A,
		const DhConfig &dh) {
	auto keyPair = Crypto::GenerateKeyPair(dh.g, dh.p);
	auto authKey = Crypto::ComputeAuthKey(
		remoteG_A,
		keyPair.privateKey,
		dh.p);

	if (authKey.empty()) {
		return;
	}

	const auto fingerprint = Crypto::ComputeFingerprint(authKey);
	const auto chatId = chat->chatId();
	const auto accessHash = chat->accessHash();

	_session->api().request(MTPmessages_AcceptEncryption(
		MTP_inputEncryptedChat(MTP_int(chatId), MTP_long(accessHash)),
		MTP_bytes(keyPair.publicComponent),
		MTP_long(fingerprint)
	)).done([=, key = std::move(authKey)](const MTPEncryptedChat &result) {
		result.match([&](const MTPDencryptedChat &data) {
			chat->setAuthKey(std::move(key));
			chat->setKeyFingerprint(fingerprint);
			chat->setState(State::Ready);
			_chatUpdated.fire(chat.get());
		}, [](const auto &) {
		});
	}).send();
}

void Manager::discardChat(int32 chatId, bool deleteHistory) {
	const auto secret = chat(chatId);
	if (!secret) {
		return;
	}
	secret->setState(State::Discarded);
	_chatUpdated.fire(secret.get());

	auto flags = MTPDmessages_discardEncryption::Flags(0);
	if (deleteHistory) {
		flags |= MTPDmessages_discardEncryption::Flag::f_delete_history;
	}
	_session->api().request(MTPmessages_DiscardEncryption(
		MTP_flags(flags),
		MTP_int(chatId)
	)).send();
}

void Manager::sendMessage(int32 chatId, const QString &text, int32 ttl) {
	const auto secret = chat(chatId);
	if (!secret || secret->state() != State::Ready) {
		return;
	}

	const auto randomId = base::RandomValue<uint64>();
	const auto messageTtl = (ttl > 0) ? ttl : secret->ttl();

	DecryptedMessage msg{
		.randomId = randomId,
		.ttl = messageTtl,
		.message = text,
	};

	const auto layerData = LayerCodec::SerializeLayer(
		secret->layer(),
		secret->inSeqNo(),
		secret->outSeqNo(),
		msg);

	const auto packet = Crypto::EncryptPacket(
		secret->keyFingerprint(),
		secret->authKey(),
		layerData,
		secret->isCreator());

	secret->advanceOutSeqNo();

	const auto inputChat = MTP_inputEncryptedChat(
		MTP_int(secret->chatId()),
		MTP_long(secret->accessHash()));

	_session->api().request(MTPmessages_SendEncrypted(
		MTP_flags(0),
		inputChat,
		MTP_long(randomId),
		MTP_bytes(packet)
	)).send();
}

void Manager::setChatTtl(int32 chatId, int32 ttl) {
	const auto secret = chat(chatId);
	if (!secret || secret->state() != State::Ready) {
		return;
	}
	secret->setTtl(ttl);

	const auto randomId = base::RandomValue<uint64>();
	DecryptedServiceMessage s{
		.randomId = randomId,
		.actionConstructor = kActionSetMessageTTLId,
		.ttlSeconds = ttl,
	};

	const auto layerData = LayerCodec::SerializeServiceLayer(
		secret->layer(),
		secret->inSeqNo(),
		secret->outSeqNo(),
		s);

	const auto packet = Crypto::EncryptPacket(
		secret->keyFingerprint(),
		secret->authKey(),
		layerData,
		secret->isCreator());

	secret->advanceOutSeqNo();

	const auto inputChat = MTP_inputEncryptedChat(
		MTP_int(secret->chatId()),
		MTP_long(secret->accessHash()));

	_session->api().request(MTPmessages_SendEncryptedService(
		inputChat,
		MTP_long(randomId),
		MTP_bytes(packet)
	)).send();

	_chatUpdated.fire(secret.get());
}

void Manager::handleUpdateEncryption(const MTPDupdateEncryption &update) {
	update.vchat().match([&](const MTPDencryptedChatWaiting &data) {
		const auto chatId = data.vid().v;
		if (const auto secret = chat(chatId)) {
			secret->setState(State::Waiting);
			_chatUpdated.fire(secret.get());
		}
	}, [&](const MTPDencryptedChatRequested &data) {
		const auto chatId = data.vid().v;
		const auto accessHash = uint64(data.vaccess_hash().v);
		const auto partnerId = UserId(data.vadmin_id().v);
		auto secret = std::make_shared<SecretChat>(
			chatId,
			accessHash,
			partnerId,
			false);
		secret->setState(State::Requested);
		secret->setAuthKey(bytes::make_vector(data.vg_a().v));
		_chats[chatId] = secret;
		_chatUpdated.fire(secret.get());

		acceptChat(chatId);
	}, [&](const MTPDencryptedChat &data) {
		const auto chatId = data.vid().v;
		if (const auto secret = chat(chatId)) {
			if (secret->isCreator() && secret->state() == State::Waiting) {
				ensureDhConfig([=, g_b = bytes::make_vector(data.vg_a_or_b().v), expectedFp = uint64(data.vkey_fingerprint().v)](const DhConfig &dh) {
					auto authKey = Crypto::ComputeAuthKey(
						g_b,
						secret->localPrivateKey(),
						dh.p);
					if (!authKey.empty()) {
						const auto computedFp = Crypto::ComputeFingerprint(authKey);
						if (computedFp == expectedFp) {
							secret->setAuthKey(std::move(authKey));
							secret->setKeyFingerprint(computedFp);
							secret->setState(State::Ready);
							_chatUpdated.fire(secret.get());
						}
					}
				});
			}
		}
	}, [&](const MTPDencryptedChatDiscarded &data) {
		const auto chatId = data.vid().v;
		if (const auto secret = chat(chatId)) {
			secret->setState(State::Discarded);
			_chatUpdated.fire(secret.get());
		}
	}, [](const MTPDencryptedChatEmpty &) {
	});
}

void Manager::handleUpdateNewEncryptedMessage(const MTPDupdateNewEncryptedMessage &update) {
	const auto parseAndProcess = [&](int32 chatId, uint64 randomId, TimeId date, bytes::const_span encryptedBytes) {
		const auto secret = chat(chatId);
		if (!secret || secret->state() != State::Ready) {
			return;
		}

		const auto decrypted = Crypto::DecryptPacket(
			secret->authKey(),
			encryptedBytes,
			secret->isCreator());
		if (!decrypted.has_value()) {
			return;
		}

		const auto layer = LayerCodec::DeserializeLayer(decrypted->plaintext);
		if (!layer.has_value()) {
			return;
		}

		secret->advanceInSeqNo();

		if (layer->message.has_value()) {
			const auto &m = *layer->message;
			_messageReceived.fire(IncomingSecretMessage{
				.chatId = chatId,
				.randomId = m.randomId ? m.randomId : randomId,
				.date = date,
				.text = m.message,
				.ttl = m.ttl,
			});
		} else if (layer->serviceMessage.has_value()) {
			const auto &s = *layer->serviceMessage;
			if (s.actionConstructor == kActionSetMessageTTLId) {
				secret->setTtl(s.ttlSeconds);
				_chatUpdated.fire(secret.get());
			} else if (s.actionConstructor == kActionNotifyLayerId && s.layer > 0) {
				secret->setLayer(std::min(s.layer, 73));
				_chatUpdated.fire(secret.get());
			}
		}
	};

	update.vmessage().match([&](const MTPDencryptedMessage &data) {
		parseAndProcess(
			data.vchat_id().v,
			uint64(data.vrandom_id().v),
			data.vdate().v,
			bytes::make_span(data.vbytes().v));
	}, [&](const MTPDencryptedMessageService &data) {
		parseAndProcess(
			data.vchat_id().v,
			uint64(data.vrandom_id().v),
			data.vdate().v,
			bytes::make_span(data.vbytes().v));
	});
}

} // namespace Ayu::Secret
