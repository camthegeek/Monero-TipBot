/*
Copyright(C) 2018 Brandan Tyler Lasley

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
GNU General Public License for more details.
*/
#include "Discord.h"
#include "Poco/JSON/Parser.h"
#include "Poco/Thread.h"
#include "RPCManager.h"
#include <fstream>
#include <memory>
#include <map>
#include <utility>

#include "cereal/archives/json.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/set.hpp"
#include "Tip.h"
#include "Faucet.h"
#include "RPCException.h"
#include "Poco/StringTokenizer.h"
#include "Lottery.h"
#include "Poco/ThreadTarget.h"

const char *aboutStr =
"```ITNS TipBot v%d.%d\\n"
"(C) Brandan Tyler Lasley 2018\\n"
"Github: https://github.com/Brandantl/IntenseCoin-TipBot \\n"
"BTC: 1KsX66J98WMgtSbFA5UZhVDn1iuhN5B6Hm\\n"
"ITNS: iz5ZrkSjiYiCMMzPKY8JANbHuyChEHh8aEVHNCcRa2nFaSKPqKwGCGuUMUMNWRyTNKewpk9vHFTVsHu32X3P8QJD21mfWJogf\\n"
"XMR: 44DudyMoSZ5as1Q9MTV6ydh4BYT6BMCvxNZ8HAgeZo9SatDVixVjZzvRiq9fiTneykievrWjrUvsy2dKciwwoUv15B9MzWS\\n```";

ITNS_TIPBOT::~ITNS_TIPBOT()
{
}

void ITNS_TIPBOT::init()
{
    Apps = { 
        {(std::shared_ptr<AppBaseClass>(std::make_unique<Tip>()))},
        {(std::shared_ptr<AppBaseClass>(std::make_unique<Faucet>()))},
        {(std::shared_ptr<AppBaseClass>(std::make_unique<Lottery>(this)))}
    };

    for (auto & app : Apps)
        app->load();
}

int ITNS_TIPBOT::getDiscordChannelType(SleepyDiscord::Snowflake<SleepyDiscord::Channel> id)
{
    Poco::JSON::Parser          parser;
    Poco::JSON::Object::Ptr     object;
    std::string                 clientID;
    auto response = getChannel(id);
    object = parser.parse(response.text).extract<Poco::JSON::Object::Ptr>();
    return object->getValue<int>("type");
}

std::string ITNS_TIPBOT::getDiscordDMChannel(DiscordID id)
{
    Poco::JSON::Parser      parser;
    Poco::JSON::Object::Ptr object;
    std::string             clientID;
    auto response = createDirectMessageChannel(Poco::format("%Lu", id));
    object = parser.parse(response.text).extract<Poco::JSON::Object::Ptr>();
    return object->getValue<std::string>("id");
}

DiscordUser UknownUser = { 0, "Unknown User", 0 };
const DiscordUser & ITNS_TIPBOT::findUser(const DiscordID & id)
{
    if (id > 0)
    {
        // Find user
        for (auto & server : UserList)
        {
            for (auto & user : server.second)
            {
                if (id == user.id) return user;
            }
        }

        // User not found... Try and pull from Discord API
        auto response = getUser(Poco::format("%?i", id));
        if (response.statusCode == 200)
        {
            Poco::JSON::Parser      parser;
            Poco::JSON::Object::Ptr object;
            object = parser.parse(response.text).extract<Poco::JSON::Object::Ptr>();

            struct DiscordUser newUser;
            newUser.username = object->getValue<std::string>("username");
            newUser.id = ITNS_TIPBOT::convertSnowflakeToInt64(object->getValue<std::string>("id"));
            newUser.join_epoch_time = ((newUser.id >> 22) + 1420070400000) * 1000;
            auto ret = UserList.begin()->second.insert(newUser);
            saveUserList();
            return *ret.first;
        }
    }

    // No idea.
    return UknownUser;
}

bool ITNS_TIPBOT::isUserAdmin(const SleepyDiscord::Message& message)
{
    auto myid = convertSnowflakeToInt64(message.author.ID);
    for (auto adminid : DiscordAdmins)
    {
        if (myid == adminid)
            return true;
    }
    return false;
}

void ITNS_TIPBOT::CommandParseError(const SleepyDiscord::Message& message, const Command& me)
{
    sendMessage(message.channelID, Poco::format("Command Error --- Correct Usage: %s %s :cold_sweat:", me.name, me.params));
}

bool ITNS_TIPBOT::isCommandAllowedToBeExecuted(const SleepyDiscord::Message& message, const Command& command, int channelType)
{
    return !command.adminTools || (command.adminTools && (channelType == AllowChannelTypes::Private || command.ChannelPermission == AllowChannelTypes::Any) && ITNS_TIPBOT::isUserAdmin(message));
}

std::string ITNS_TIPBOT::generateHelpText(const std::string & title, const std::vector<Command>& cmds, int ChannelType, const SleepyDiscord::Message& message)
{
    std::stringstream ss;
    ss << title;
    ss << "```";
    for (auto cmd : cmds)
    {
        if (ITNS_TIPBOT::isCommandAllowedToBeExecuted(message, cmd, ChannelType))
        {
            ss << cmd.name << " " << cmd.params;
            if (cmd.ChannelPermission != AllowChannelTypes::Any)
            {
                ss << " -- " << AllowChannelTypeNames[cmd.ChannelPermission];
            }
            if (cmd.adminTools)
            {
                ss << " -- ADMIN ONLY";
            }
            ss << "\\n";
        }
    }
    ss << "```";
    return ss.str();
}

void dispatcher(const std::function<void(ITNS_TIPBOT *, const SleepyDiscord::Message &, const Command &)> & func, ITNS_TIPBOT * DiscordPtr, const SleepyDiscord::Message & message, const struct Command & me)
{
    try
    {
        func(DiscordPtr, message, me);
    }
    catch (const Poco::Exception & exp)
    {
        DiscordPtr->sendMessage(message.channelID, "Poco Error: ---" + std::string(exp.what()) + " :cold_sweat:");
    }
    catch (const SleepyDiscord::ErrorCode & exp)
    {
        std::cerr << Poco::format("Discord Error Code: --- %d\n", exp);
    }
    catch (AppGeneralException & exp)
    {
        DiscordPtr->sendMessage(message.channelID, std::string(exp.what()) + " --- " + exp.getGeneralError() + " :cold_sweat:");
    }
}

void ITNS_TIPBOT::onMessage(SleepyDiscord::Message message)
{
    // Dispatcher
    if (!message.content.empty() && message.content.at(0) == '!')
    {
        int channelType;
        for (const auto & ptr : Apps)
        {
            for (const auto & command : *ptr.get())
            {
                try
                {
                    Poco::StringTokenizer cmd(message.content, " ");

                    if (command.name == Poco::toLower(cmd[0]))
                    {
                        channelType = getDiscordChannelType(message.channelID);
                        if ((command.ChannelPermission == AllowChannelTypes::Any) || (channelType == command.ChannelPermission))
                        {
                            if (command.opensWallet)
                                ptr->setAccount(&RPCMan.getAccount(convertSnowflakeToInt64(message.author.ID)));
                            else  ptr->setAccount(nullptr);

                            if (ITNS_TIPBOT::isCommandAllowedToBeExecuted(message, command, channelType))
                            {
                                // Create command thread
                                std::thread t1(dispatcher, command.func, this, message, command);
                                t1.detach();
                            }
                        }
                        break;
                    }
                }
                catch (const Poco::Exception & exp)
                {
                    sendMessage(message.channelID, "Poco Error: ---" + std::string(exp.what()) + " :cold_sweat:");
                }
                catch (const SleepyDiscord::ErrorCode & exp)
                {
                    std::cerr << Poco::format("Discord Error Code: --- %d\n", exp);
                }
                catch (AppGeneralException & exp)
                {
                    sendMessage(message.channelID, std::string(exp.what()) + " --- " + exp.getGeneralError() + " :cold_sweat:");
                }
            }
        }
    }
}

#define DISCORD_MAX_GET_USERS 1000
void getDiscordUsers(ITNS_TIPBOT & me, std::set<DiscordUser> & myList, const SleepyDiscord::Snowflake<SleepyDiscord::Server> & snowyServer, const unsigned short & limit, const SleepyDiscord::Snowflake<SleepyDiscord::User> & snowyUser)
{
    auto guildInfo = me.listMembers(snowyServer, limit, snowyUser).vector();

    for (auto user : guildInfo)
    {
        struct DiscordUser newUser;
        newUser.username = user.user.username;
        newUser.id = ITNS_TIPBOT::convertSnowflakeToInt64(user.user.ID);
        newUser.join_epoch_time = ((newUser.id >> 22) + 1420070400000) * 1000;
        myList.insert(newUser);
    }

    if (guildInfo.size() == limit)
    {
        Poco::Thread::sleep(3000); // Wait a bit.
        getDiscordUsers(me, myList, snowyServer, limit, guildInfo[limit - 1].user.ID);
    }
}

void ITNS_TIPBOT::onReady(SleepyDiscord::Ready readyData)
{
    loadUserList();
    BotUser = readyData.user;
    RPCMan.setBotUser(convertSnowflakeToInt64(BotUser.ID));
    refreshUserList();
}

void ITNS_TIPBOT::refreshUserList()
{
    auto servs = this->getServers().vector();

    std::cout << "Loading Discord Users...\n";
    for (auto serv : servs)
    {
        Poco::Thread::sleep(3000); // Wait a bit.
        if (UserList[convertSnowflakeToInt64(serv.ID)].empty())
        {
            getDiscordUsers(*this, UserList[convertSnowflakeToInt64(serv.ID)], serv.ID, DISCORD_MAX_GET_USERS, "");
        }
        else
        {
            getDiscordUsers(*this, UserList[convertSnowflakeToInt64(serv.ID)], serv.ID, DISCORD_MAX_GET_USERS, Poco::format("%?i", UserList[convertSnowflakeToInt64(serv.ID)].rbegin()->id));
        }
        std::cout << "Saving Discord Users To Disk...\n";
        saveUserList();
    }
    std::cout << "Discord Users Load Completed... Ready!\n";
}

void ITNS_TIPBOT::saveUserList()
{
    std::ofstream out(DISCORD_USER_CACHE_FILENAME, std::ios::trunc);
    if (out.is_open())
    {
        std::cout << "Saving wallet data to disk...\n";
        {
            cereal::JSONOutputArchive ar(out);
            ar(CEREAL_NVP(UserList));
        }
        out.close();
    }
}

const struct TopTakerStruct ITNS_TIPBOT::findTopTaker()
{
    TopTakerStruct me;
    std::map<DiscordID, std::uint64_t> topTakerList;
    for (const auto & mp : UserList)
    {
        for (const auto & usr : mp.second)
        {
            topTakerList[usr.id] += usr.total_faucet_itns_sent;
        }
    }

    auto TopTaker = std::max_element(topTakerList.begin(), topTakerList.end(),
        [](const std::pair<DiscordID, std::uint64_t>& p1, const std::pair<DiscordID, std::uint64_t>& p2) {
        return p1.second < p2.second; });

    const auto & TopDonorUser = findUser(TopTaker->first);

    me.me = TopDonorUser;
    me.amount = TopTaker->second;
    return me;
}

void ITNS_TIPBOT::AppSave()
{
    for (auto & app : Apps)
        app->save();
}

std::uint64_t ITNS_TIPBOT::totalFaucetAmount()
{
    std::uint64_t amount = 0;
    for (auto & server : UserList)
    {
        for (auto & user : server.second)
        {
            amount += user.total_faucet_itns_sent;
        }
    }
    return amount;
}

void ITNS_TIPBOT::loadUserList()
{
    std::ifstream in(DISCORD_USER_CACHE_FILENAME);
    if (in.is_open())
    {
        std::cout << "Saving wallet data to disk...\n";
        {
            cereal::JSONInputArchive ar(in);
            ar(CEREAL_NVP(UserList));
        }
        in.close();
    }
}
