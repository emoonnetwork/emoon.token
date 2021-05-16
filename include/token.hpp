#pragma once
#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>

#include <string>
#include <cmath>

#include <string>

using std::string;
using namespace eosio;


class [[eosio::contract("token")]] token : public contract {
public:
   static constexpr name MARKET_ACCOUNT  { name("emoonfinance") };
   static constexpr name AIRDROP_ACCOUNT  { name("emoonairdrop") };
   static const uint64_t PAIR_ID = 1482;
   static const uint64_t EPOCH_TIME = 1621069200;

   using contract::contract;

   /**
    * Create action.
    *
    * @details Allows `issuer` account to create a token in supply of `maximum_supply`.
    * @param issuer - the account that creates the token,
    * @param maximum_supply - the maximum supply set for the token created.
    *
    * @pre Token symbol has to be valid,
    * @pre Token symbol must not be already created,
    * @pre maximum_supply has to be smaller than the maximum supply allowed by the system: 1^62 - 1.
    * @pre Maximum supply must be positive;
    *
    * If validation is successful a new entry in statstable for token symbol scope gets created.
    */
   [[eosio::action]] 
   void create(const name &issuer, const asset &maximum_supply);


   /**
    * Issue action.
    *
    * @details This action issues to `to` account a `quantity` of tokens.
    *
    * @param to - the account to issue tokens to, it must be the same as the issuer,
    * @param quntity - the amount of tokens to be issued,
    * @memo - the memo string that accompanies the token issue transaction.
    */
   [[eosio::action]] 
   void issue(const name &to, const asset &quantity, const string &memo);

   /**
    * Retire action.
    *
    * @details The opposite for create action, if all validations succeed,
    * it debits the statstable.supply amount.
    *
    * @param quantity - the quantity of tokens to retire,
    * @param memo - the memo string to accompany the transaction.
    */
   [[eosio::action]] 
   void retire(const asset &quantity, const string &memo);

   /**
    * Transfer action.
    *
    * @details Allows `from` account to transfer to `to` account the `quantity` tokens.
    * One account is debited and the other is credited with quantity tokens.
    *
    * @param from - the account to transfer from,
    * @param to - the account to be transferred to,
    * @param quantity - the quantity of tokens to be transferred,
    * @param memo - the memo string to accompany the transaction.
    */
   [[eosio::action]] 
   void transfer(const name &from, const name &to, const asset &quantity, const string &memo);
   /**
    * Open action.
    *
    * @details Allows `ram_payer` to create an account `owner` with zero balance for
    * token `symbol` at the expense of `ram_payer`.
    *
    * @param owner - the account to be created,
    * @param symbol - the token to be payed with by `ram_payer`,
    * @param ram_payer - the account that supports the cost of this action.
    *
    * More information can be read [here](https://github.com/EOSIO/eosio.contracts/issues/62)
    * and [here](https://github.com/EOSIO/eosio.contracts/issues/61).
    */
   [[eosio::action]] 
   void open(const name &owner, const symbol &symbol, const name &ram_payer);

   /**
    * Close action.
    *
    * @details This action is the opposite for open, it closes the account `owner`
    * for token `symbol`.
    *
    * @param owner - the owner account to execute the close action for,
    * @param symbol - the symbol of the token to execute the close action for.
    *
    * @pre The pair of owner plus symbol has to exist otherwise no action is executed,
    * @pre If the pair of owner plus symbol exists, the balance has to be zero.
    */
   [[eosio::action]] 
   void close(const name &owner, const symbol &symbol);

   [[eosio::action]] 
   void addliquidity();

   [[eosio::action]] 
   void stake(const name &owner, const asset &quantity);

   [[eosio::action]] 
   void unstake(const name &owner, const symbol_code sym, uint128_t token);

   [[eosio::action]] 
   void claim(const name &owner, const symbol_code sym);

   /**
    * Get supply method.
    *
    * @details Gets the supply for token `sym_code`, created by `token_contract_account` account.
    *
    * @param token_contract_account - the account to get the supply for,
    * @param sym_code - the symbol to get the supply for.
    */
   static asset get_supply(const name &token_contract_account, const symbol_code &sym_code) {
      stats statstable(token_contract_account, sym_code.raw());
      const auto &st = statstable.get(sym_code.raw());
      return st.supply;
   }

   /**
    * Get balance method.
    *
    * @details Get the balance for a token `sym_code` created by `token_contract_account` account,
    * for account `owner`.
    *
    * @param token_contract_account - the token creator account,
    * @param owner - the account for which the token balance is returned,
    * @param sym_code - the token for which the balance is returned.
    */
   static asset get_balance(const name &token_contract_account, const name &owner, const symbol_code &sym_code) {
      accounts accountstable(token_contract_account, owner.value);
      const auto &ac = accountstable.get(sym_code.raw());
      return ac.balance;
   }

private:
   struct [[eosio::table]] account {
      asset balance;

      uint64_t primary_key() const { return balance.symbol.code().raw(); }
   };

   struct [[eosio::table]] currency_stats {
      asset supply;
      asset max_supply;
      name issuer;

      uint64_t primary_key() const { return supply.symbol.code().raw(); }
   };

    struct [[eosio::table]] currency_stats2 {
      asset total_principal;
      uint128_t total_token;

      uint64_t primary_key() const { return total_principal.symbol.code().raw(); }
   };

   struct [[eosio::table]] token_stake {
      name owner;
      asset principal;
      uint128_t token;

      uint64_t primary_key() const { return owner.value; }
   };

   typedef eosio::multi_index<"accounts"_n, account> accounts;
   typedef eosio::multi_index<"stat"_n, currency_stats> stats;
   typedef eosio::multi_index<"stat2"_n, currency_stats2> stats2;
   typedef eosio::multi_index<"stakes"_n, token_stake> stakes;

   void _claimunstake(const name &owner, const symbol_code sym, uint128_t token);

   void sub_balance(const name &owner, const asset &value);
   const asset& add_balance(const name &owner, const asset &value, const name &ram_payer);

   bool is_whitelist(const name &account);

};