// RawAmiClientListener.hpp
#pragma once

#include <string>
#include <map>
#include "AmiTypes.hpp"

namespace ami {
    class RawAmiClient;

    class RawAmiClientListener {
    public:
        virtual ~RawAmiClientListener() = default;
        virtual void onConnect(RawAmiClient* client) = 0;
        virtual void onDisconnect(RawAmiClient* client) = 0;
        virtual void onMessageReceived(RawAmiClient* client,
            long ts,
            long seqNum,
            int status,
            const std::string& message) = 0;
        virtual void onMessageSent(RawAmiClient* client,
            const std::string& message) = 0;
        virtual void onCommand(RawAmiClient* client,
            const std::string& requestId,
            const std::string& cmd,
            const std::string& userName,
            const std::string& objectType,
            const std::string& objectId,
            const std::map<std::string, AmiValue>& params) = 0;
        virtual void onLoggedIn(RawAmiClient* client) = 0;
    };

}// RAW_AMI_CLIENT_LISTENER_HPP
