/*
 * This is the source code of AyuGram for Desktop.
 *
 * We do not and cannot prevent the use of our code,
 * but be respectful and credit the original author.
 *
 * Copyright @Radolyn, 2026
 */
#pragma once

#include "ayu/secret/secret_chat.h"
#include "ayu/secret/secret_chat_crypto.h"
#include "ayu/secret/secret_chat_layer.h"
#include "base/bytes.h"
#include "rpl/event_stream.h"
#include "rpl/lifetime.h"

#include <map>
#include <memory>
#include <optional>

namespace Main {
class Session;
} // namespace Main

class UserData;
class PeerData;
class HistoryItem;

namespace Ayu::Secret {

struct IncomingSecretMessage {
	int32 chatId = 0;
	uint64 randomId = 0;
	TimeId date = 0;
	QString text;
	int32 ttl = 0;
};

class Manager final {
public:
	explicit Manager(not_null<Main::Session*> session);
	~Manager();

	void requestChat(not_null<UserData*> user);
	void acceptChat(int32 chatId);
	void discardChat(int32 chatId, bool deleteHistory = true);

	void sendMessage(int32 chatId, const QString &text, int32 ttl = 0);
	void sendMedia(int32 chatId, not_null<HistoryItem*> item, const QString &caption);
	void setChatTtl(int32 chatId, int32 ttl);

	[[nodiscard]] std::shared_ptr<SecretChat> chat(int32 chatId) const;
	[[nodiscard]] std::shared_ptr<SecretChat> chatForUser(UserId userId) const;
	[[nodiscard]] const std::map<int32, std::shared_ptr<SecretChat>> &chats() const;

	void handleUpdateEncryption(const MTPDupdateEncryption &update);
	void handleUpdateNewEncryptedMessage(const MTPDupdateNewEncryptedMessage &update);

	[[nodiscard]] rpl::producer<not_null<SecretChat*>> chatUpdated() const;
	[[nodiscard]] rpl::producer<IncomingSecretMessage> messageReceived() const;

private:
	void ensureDhConfig(Fn<void(const DhConfig &)> callback);
	void finishAccept(
		const std::shared_ptr<SecretChat> &chat,
		bytes::const_span remoteG_A,
		const DhConfig &dh);

	void loadSessionsFromDb();
	void saveSessionToDb(const std::shared_ptr<SecretChat> &chat);
	void saveMessageToDb(
		int32 chatId,
		uint64 randomId,
		uint64 senderId,
		TimeId date,
		const QString &text,
		int32 ttl);
	void insertLocalMessage(
		not_null<PeerData*> partnerPeer,
		MsgId msgId,
		TimeId date,
		const QString &text,
		bool isOut,
		int32 ttl);

	const not_null<Main::Session*> _session;
	std::map<int32, std::shared_ptr<SecretChat>> _chats;
	std::optional<DhConfig> _dhConfig;
	rpl::event_stream<not_null<SecretChat*>> _chatUpdated;
	rpl::event_stream<IncomingSecretMessage> _messageReceived;
	rpl::lifetime _lifetime;

};

} // namespace Ayu::Secret
