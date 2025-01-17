/*
 *  Copyright (C) 2021-2023 Savoir-faire Linux Inc.
 *
 *  Author: Sébastien Blin <sebastien.blin@savoirfairelinux.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.
 */

#include "jamidht/conversation_channel_handler.h"

namespace jami {

ConversationChannelHandler::ConversationChannelHandler(const std::shared_ptr<JamiAccount>& acc,
                                                       ConnectionManager& cm)
    : ChannelHandlerInterface()
    , account_(acc)
    , connectionManager_(cm)
{}

ConversationChannelHandler::~ConversationChannelHandler() {}

void
ConversationChannelHandler::connect(const DeviceId& deviceId,
                                    const std::string& channelName,
                                    ConnectCb&& cb)
{
    connectionManager_.connectDevice(deviceId,
                                     "git://" + deviceId.toString() + "/" + channelName,
                                     [cb = std::move(cb)](std::shared_ptr<ChannelSocket> socket,
                                                          const DeviceId& dev) {
                                         if (cb)
                                             cb(socket, dev);
                                     });
}

bool
ConversationChannelHandler::onRequest(const std::shared_ptr<dht::crypto::Certificate>&,
                                      const std::string& name)
{
    // Pre-check before acceptance. Sometimes, another device can start a conversation
    // which is still not synced. So, here we decline channel's request in this case
    // to avoid the other device to want to sync with us if we are not ready.
    auto sep = name.find_last_of('/');
    auto conversationId = name.substr(sep + 1);
    auto remoteDevice = name.substr(6, sep - 6);
    if (auto acc = account_.lock())
        if (auto convModule = acc->convModule()) {
            auto res = !convModule->isBannedDevice(conversationId, remoteDevice);
            return res;
        }
    return false;
}

void
ConversationChannelHandler::onReady(const std::shared_ptr<dht::crypto::Certificate>&,
                                    const std::string&,
                                    std::shared_ptr<ChannelSocket>)
{}

} // namespace jami