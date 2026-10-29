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
		// Save g_a temporarily in authKey until accept
		secret->setAuthKey(bytes::make_vector(data.vg_a().v));
		_chats[chatId] = secret;
		_chatUpdated.fire(secret.get());

		// Auto-accept incoming secret chat invitation
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

} // namespace Ayu::Secret
