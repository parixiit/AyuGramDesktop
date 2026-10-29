/*
 * This is the source code of AyuGram for Desktop.
 *
 * We do not and cannot prevent the use of our code,
 * but be respectful and credit the original author.
 *
 * Copyright @Radolyn, 2026
 */
#include "ayu/secret/secret_chat_manager.h"

#include "ayu/data/ayu_database.h"
#include "ayu/data/entities.h"
#include "base/unixtime.h"
#include "data/data_session.h"
#include "data/data_peer_id.h"
#include "history/history.h"

#include "apiwrap.h"
#include "base/random.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "mtproto/mtproto_dh_utils.h"

namespace Ayu::Secret {

Manager::Manager(not_null<Main::Session*> session)
: _session(session) {
	loadSessionsFromDb();
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
			saveSessionToDb(chat);
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

	if (deleteHistory) {
		AyuDatabase::deleteSecretChatSession(chatId);
	} else {
		saveSessionToDb(secret);
	}

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
	saveSessionToDb(secret);
	saveMessageToDb(
		chatId,
		randomId,
		_session->userId().bare,
		base::unixtime::now(),
		text,
		messageTtl);
	const auto partnerUser = _session->data().user(secret->partnerId());
	insertLocalMessage(
		partnerUser,
		_session->data().nextLocalMessageId(),
		base::unixtime::now(),
		text,
		true,
		messageTtl);

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

	saveSessionToDb(secret);
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
							saveSessionToDb(secret);
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
			const auto effectiveRandomId = m.randomId ? m.randomId : randomId;
			saveSessionToDb(secret);
			saveMessageToDb(
				chatId,
				effectiveRandomId,
				secret->partnerId().bare,
				date,
				m.message,
				m.ttl);
			const auto partnerUser = _session->data().user(secret->partnerId());
			insertLocalMessage(
				partnerUser,
				_session->data().nextLocalMessageId(),
				date,
				m.message,
				false,
				m.ttl);
			_messageReceived.fire(IncomingSecretMessage{
				.chatId = chatId,
				.randomId = effectiveRandomId,
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


void Manager::loadSessionsFromDb() {
	const auto sessions = AyuDatabase::getSecretChatSessions(_session->userId().bare);
	for (const auto &s : sessions) {
		auto chat = std::make_shared<SecretChat>(
			s.chatId,
			uint64(s.accessHash),
			UserId(s.partnerId),
			s.isCreator);
		chat->setState(static_cast<State>(s.state));
		if (!s.authKey.empty()) {
			auto keyBytes = bytes::make_vector(bytes::make_span(s.authKey));
			chat->setAuthKey(std::move(keyBytes));
		}
		chat->setKeyFingerprint(s.keyFingerprint);
		chat->setLayer(s.layer);
		chat->setTtl(s.ttl);
		_chats[s.chatId] = chat;

		const auto messages = AyuDatabase::getSecretChatMessages(s.chatId, 50);
		const auto partnerUser = _session->data().user(s.partnerId);
		for (const auto &msg : messages) {
			const auto isOut = (msg.senderId == _session->userId().bare);
			insertLocalMessage(
				partnerUser,
				_session->data().nextLocalMessageId(),
				msg.date,
				QString::fromStdString(msg.text),
				isOut,
				msg.ttl);
		}
	}
}

void Manager::saveSessionToDb(const std::shared_ptr<SecretChat> &chat) {
	if (!chat) {
		return;
	}
	SecretChatSessionEntity entity;
	entity.chatId = chat->chatId();
	entity.userId = _session->userId().bare;
	entity.accessHash = chat->accessHash();
	entity.partnerId = chat->partnerId().bare;
	entity.isCreator = chat->isCreator();
	entity.state = static_cast<int>(chat->state());
	const auto &key = chat->authKey();
	entity.authKey = std::vector<char>(key.data(), key.data() + key.size());
	entity.keyFingerprint = chat->keyFingerprint();
	entity.inSeqNo = chat->inSeqNo();
	entity.outSeqNo = chat->outSeqNo();
	entity.layer = chat->layer();
	entity.ttl = chat->ttl();
	entity.createdAt = base::unixtime::now();
	AyuDatabase::saveSecretChatSession(entity);
}

void Manager::saveMessageToDb(
		int32 chatId,
		uint64 randomId,
		uint64 senderId,
		TimeId date,
		const QString &text,
		int32 ttl) {
	SecretChatMessageEntity m;
	m.chatId = chatId;
	m.randomId = randomId;
	m.senderId = senderId;
	m.date = date;
	m.text = text.toStdString();
	m.ttl = ttl;
	m.destroyAt = (ttl > 0) ? (date + ttl) : 0;
	m.inSeqNo = 0;
	m.outSeqNo = 0;
	AyuDatabase::saveSecretChatMessage(m);
}


void Manager::insertLocalMessage(
		not_null<PeerData*> partnerPeer,
		MsgId msgId,
		TimeId date,
		const QString &text,
		bool isOut,
		int32 ttl) {
	const auto history = _session->data().history(partnerPeer);

	using Flag = MTPDmessage::Flag;
	auto flags = Flag::f_from_id;
	if (isOut) {
		flags |= Flag::f_out;
	}
	if (ttl > 0) {
		flags |= Flag::f_ttl_period;
	}

	const auto fromPeer = isOut ? _session->user() : partnerPeer;
	const auto mtpMsg = MTP_message(
		MTP_flags(flags),
		MTP_int(msgId.bare),
		peerToMTP(fromPeer->id),
		MTPint(),
		MTPstring(),
		peerToMTP(partnerPeer->id),
		MTPPeer(),
		MTPMessageFwdHeader(),
		MTPlong(),
		MTPlong(),
		MTPPeer(),
		MTPMessageReplyHeader(),
		MTP_int(date ? date : base::unixtime::now()),
		MTP_string(text),
		MTPMessageMedia(),
		MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(),
		MTPint(),
		MTPint(),
		MTPMessageReplies(),
		MTPint(),
		MTPstring(),
		MTPlong(),
		MTPMessageReactions(),
		MTPVector<MTPRestrictionReason>(),
		(ttl > 0 ? MTP_int(ttl) : MTPint()),
		MTPint(),
		MTPlong(),
		MTPFactCheck(),
		MTPint(),
		MTPlong(),
		MTPSuggestedPost(),
		MTPint(),
		MTPstring(),
		MTPRichMessage());

	history->addNewMessage(
		msgId,
		mtpMsg,
		MessageFlag::HasFromId | (isOut ? MessageFlag::Outgoing : MessageFlag(0)),
		isOut ? NewMessageType::Existing : NewMessageType::Unread);
}

} // namespace Ayu::Secret
