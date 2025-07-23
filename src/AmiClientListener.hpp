// AmiClientListener.hpp
#pragma once
#include <string>
#include <map>
#include <vector>
#include <memory>
#include "AmiTypes.hpp"

class AmiClient;

/**
 * Listener interface for high-level AMI client events.
 */
class AmiClientListener {
public:
    virtual ~AmiClientListener() = default;
    virtual void onConnect(AmiClient* client) = 0;
    virtual void onDisconnect(AmiClient* client) = 0;
    virtual void onLoggedIn(AmiClient* client) = 0;
    virtual void onMessageReceived(AmiClient* client,
        long long timestamp,
        long long seqNum,
        int status,
        const std::string& message) = 0;
    virtual void onMessageSent(AmiClient* client,
        const std::string& message) = 0;
    virtual void onCommand(AmiClient* client,
        const std::string& requestId,
        const std::string& cmd,
        const std::string& userName,
        const std::string& objectType,
        const std::string& objectId,
        const std::map<std::string, AmiValue>& params) = 0;
};
