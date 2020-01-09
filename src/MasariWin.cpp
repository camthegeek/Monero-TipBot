#include "MasariWin.h"
#include "Tipbot.h"
#include "Poco/DateTimeFormatter.h"
#include "Poco/Timestamp.h"
#include "Poco/Timespan.h"
#include "RPCManager.h"
#include <utility>
#include <map>
#include "Config.h"
#include "RPCException.h"
#include "Language.h"
#include <fstream>
//#include "cereal/cereal.hpp"
//#include "cereal/archives/json.hpp"
#include <Poco/StringTokenizer.h>
#include <string>
#include <regex>
#include "cpr/cpr.h"
#include <nlohmann/json.hpp>

// for convenience
using json = nlohmann::json;

#define CLASS_RESOLUTION(x) std::bind(&Bet::x, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
Bet::Bet() : MyAccount(nullptr)
{
   Commands =
    {
        // User Commands 
        // Command            Function                                       Params                              Wallet  Admin   Allowed Channel
        { "!bet",           CLASS_RESOLUTION(MakeBet),                       "[amount] [character (a-f 0-9)]",                    false,   false,  AllowChannelTypes::Any },
        { "!penile",           CLASS_RESOLUTION(Help),                       "",                    false,   false,  AllowChannelTypes::Any },

    };
}

void Bet::save()
{
}

void Bet::load()
{
}
void Bet::setAccount(Account* acc)
{
    this->MyAccount = acc;
}
iterator Bet::begin()
{
    return Commands.begin();
}

const_iterator Bet::begin() const
{
    return Commands.begin();
}

const_iterator Bet::cbegin() const
{
    return Commands.cbegin();
}

iterator Bet::end()
{
    return Commands.end();
}

const_iterator Bet::end() const
{
    return Commands.end();
}

const_iterator Bet::cend() const
{
    return Commands.cend();
}

void Bet::Help(TIPBOT * DiscordPtr, const UserMessage& message, const struct Command & me)
{
    const auto helpStr = TIPBOT::generateHelpText(GETSTR(DiscordPtr->getUserLang(message.User.id), "PROJECTS_HELP"), Commands, message);
    DiscordPtr->SendMsg(message, helpStr);
}

// everything above this line is based off code from other portions of this tipbot.

// begin a novice c++ developers journey into recreating a module he once made in nodejs.

// check to make sure only unique characters in a string.
// reference: https://www.geeksforgeeks.org/determine-string-unique-characters/
// thanks google!
bool uniqueCharacters(std::string str1) { 
    for (int i = 0; i < str1.length() - 1; i++) { 
        for (int j = i + 1; j < str1.length(); j++) { 
            if (str1[i] == str1[j]) { 
                return false; 
            } 
        } 
    } 
    return true; 
}


// cleans off quotes (double quotes);
// another thanks to google.. maybe stackoverflow idk 2am coding.
std::string quoteCleaner(std::string yadda) { 
    if (yadda.front() == '"') {
        yadda.erase(0,1);
        yadda.erase(yadda.size() -1);
    }
    return yadda;
}

// viewBet details: 
// purpose: return bet data after placed.
// viewBet(paymentID)
// * paymentID is from masari.win. it is always unique, refers to wallet paymentID and game ID.
std::string viewBet(std::string paymentID) {
    std::cout << "Payment ID in viewBet: " << paymentID << std::endl;
	auto r = cpr::Get(cpr::Url{"https://masari.win/api/game/"+paymentID});
	std::cout << r.text << std::endl;
    return r.text;
}

// placeBet details:
// * work in progress
// purpose:places a bet on masari.win based on discord user input.
// format: bet [amount] [guess]
// firing order:
// if amount is valid > 5 < 100 [done]
// * if guess is valid ([a-zA-Z0-9] and only 1 of each) [done]
// * * if balance.available > [amount] [done]
// * * * place the bet [done]
// * * * * wait for callback to get the send address [done]
// * * * * * do transfer to send address
// * * * * * * send message, bet placed, give link to bet.
// perhaps we can write ACTIVE bets to an activebets.json?
// perhaps we can write WINNING bets to winningbets.json?
// perhaps we can write LOSING bets to losingbets.json?
// reason for writing bets to file, send notifications (via pm) when a user wins or loses..
std::string placeBet(std::string guess, std::string address) {
    // get length of guess, require at masari.win
	std::string letlength = std::to_string(guess.length());
    // create json object
	json bet = {
        {"guess", guess},
        {"returnAddress", address},
        {"length", letlength}
    };
    // post to site
	auto req = cpr::Post(cpr::Url{"https://masari.win/api/game"},
     					cpr::Body{bet.dump()}, // post json into body
                        cpr::Header{{"Content-Type", "application/json"}}); // always set your content type when posting data.
    return req.text; // return result
}




void Bet::MakeBet(TIPBOT * DiscordPtr, const UserMessage& message, const struct Command & me)
{ 
    Poco::StringTokenizer cmd(message.Message, " ");
    if (cmd.count() != 3) // make sure at least 3 params (bet amount guess, if not, bitch about it)
            DiscordPtr->CommandParseError(message, me);
    else // if 3, lets go on. . .
    {
        const auto amount = Poco::NumberParser::parseFloat(cmd[1]); // amount we're betting
        const auto betChars = cmd[2]; // our guess
        const std::regex rgxp("^[0-9a-fA-F]+$"); // make sure only 0-9, a-F characters!
        std::smatch match; // idk what im doing
        if (amount < 5 || amount > 100) { // but I know what i'm doing here - bitch if amount is less than 5 or more than 100
            DiscordPtr->SendMsg(message, Poco::format(GETSTR(DiscordPtr->getUserLang(message.User.id), "BET_AMOUNT_ERROR"), message.User.id, amount, GlobalConfig.RPC.coin_abbv));
            return; // and quit after bitching.
        } 
        if (betChars.length() > 15) { // if our guess is more than the supported amount in a bet, bitch about it.
            DiscordPtr->SendMsg(message, Poco::format(GETSTR(DiscordPtr->getUserLang(message.User.id), "BET_CHARLEN_ERROR"), message.User.id, betChars, GlobalConfig.RPC.coin_abbv));
            return; // we stop bitching about guess length here.
        }
        if (!std::regex_match(betChars, match, rgxp)) { // if letters go beyond a-f (j-z), bitch about it.
            DiscordPtr->SendMsg(message, Poco::format(GETSTR(DiscordPtr->getUserLang(message.User.id), "BET_INVALIDBETCHARS_ERROR"), message.User.id, betChars, GlobalConfig.RPC.coin_abbv));
            return; // stop bitching about the letters here.
        } 
        if (!uniqueCharacters(betChars)) { // make sure no character is the same. if so, bitch about it.
            DiscordPtr->SendMsg(message, Poco::format(GETSTR(DiscordPtr->getUserLang(message.User.id), "BET_MULTIPLEBETCHARS_ERROR"), message.User.id, betChars, GlobalConfig.RPC.coin_abbv));
            return; // shut up about same letters here.
        }
        if (uniqueCharacters(betChars)) { // if it's all unique letters
            if (std::regex_match(betChars, match, rgxp)) { // and it passed our regexp for making sure it's 0-9 and a-F only
                const auto myWallet = Account::getWalletAddress(message.User.id); // lets get my wallet address (not camthegeeks, but.. the user)
                const auto bal = RPCMan->getTotalBalance(); // and lets pull their balance
                const auto UnlockedBalance = bal.UnlockedBalance / GlobalConfig.RPC.coin_offset; // get unlocked only, convert to decimal.
                std::cout << "Unlocked balance: " << UnlockedBalance << std::endl;
                if (UnlockedBalance >= amount) {  // now if their balance is more than the bet amount, we'll keep going!
                   std::string madeBet = placeBet(betChars, myWallet); // MAKE THE BET BABY!

                   const auto response1 = json::parse(madeBet); // parse the bet response in json. . .
                   std::string paymentID = quoteCleaner((response1["paymentId"]).dump()); // get the paymentID from response and remove double quotes around it
                   std::string gameURL = "https://masari.win/#/game/"+paymentID; // set game url to be sent for viewing pleasure
                   std::string getDetails = viewBet(paymentID); // use that paymentID to get full game details
                   const auto betDetails = json::parse(getDetails); // parse those game details as json

                   std::cout << "bet details: " << betDetails << std::endl;

                   // start setting variables for all the things we're gonna need
                   const auto& sendAddress = quoteCleaner((betDetails["paymentAddress"]).dump()); // get our send to destination

                   std::string winChance = quoteCleaner((betDetails["winChance"]).dump()); // get our chances of winning
                   std::string xmultiplier = quoteCleaner((betDetails["multiplier"]).dump()); // get the multipler
                   xmultiplier.erase(xmultiplier.size() -1); //trim the last character (x) off of xmultilplier
                   std::cout << "multiplier: " << xmultiplier << std::endl;

                   int y = stoi(xmultiplier); // convert xmultiplier to integer so we can do math with it
                   const auto amountToBeWon = amount * y;

                   std::cout << "multiplier total: " << amount * y << std::endl;

                   // lets wrap this shit show up
                   std::cout << "- - - - - - " << std::endl 
                   << " DETAILS ABOUT THE BET " << std::endl 
                   << "My wallet: " << myWallet << std::endl
                   << "My balance: " << bal.UnlockedBalance << std::endl 
                   << "My bet: " << amount << " on " << betChars << std::endl 
                   << "My chances: " << winChance << std::endl 
                   << "Send to: " << sendAddress << std::endl;


                   std::cout << typeid(static_cast<uint64_t>(amount)).name() << std::endl;

                   // alright alright alright -- we've got everything we need.
                   // now we just need to construct the transaction and send it

                   // I get thread/mutex lock issues when trying to autosend funds. Someone more expeirenced in C++ can tackle that function.

                   // future features:
                   // we need to figure out a way to save bets to a file (activebets.json?) and announce winning bets in PM?
                   // this active bets file will store the betDetails['confirmed'] (funds received) as well as betDetails['complete'] (true/false).
                   // if complete = true, remove from activebets. 
                   // if won or lost, make the announcement about it?                                                       
                   
                   DiscordPtr->SendMsg(message, Poco::format(GETSTR(DiscordPtr->getUserLang(message.User.id), "BET_MADE"), message.User.id, betChars, winChance, amountToBeWon, GlobalConfig.RPC.coin_abbv, amount, GlobalConfig.RPC.coin_abbv, sendAddress, gameURL));
               }
               else {
                std::cout << "Not enough money shitface";
               }
            }
        }
    }
}

 