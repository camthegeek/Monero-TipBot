#pragma once
#include "Tipbot.h"
#include "Account.h"
#include "Util.h"
#include "AppBaseClass.h"
#include "Poco/Logger.h"
#include "Poco/AutoPtr.h"

class TIPBOT;

class Bet : public AppBaseClass
{
public:
    Bet();
    ~Bet() = default;

    void                                save();
    void                                load();
    
    iterator                            begin();
    const_iterator                      begin() const;
    const_iterator                      cbegin() const;

    iterator                            end();
    const_iterator                      end() const;
    const_iterator                      cend() const;

    void                                setAccount(Account *);
    void                                Help(TIPBOT * DiscordPtr, const UserMessage& message, const struct Command & me);
    void                                MakeBet(TIPBOT * DiscordPtr, const UserMessage& message, const struct Command & me);
private:
    std::vector<struct Command>     Commands;
    Account*                        MyAccount;

};