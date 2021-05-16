#include <token.hpp>
#include <swap.hpp>

void token::create(const name &issuer, const asset &maximum_supply) {
   require_auth(get_self());

   auto sym = maximum_supply.symbol;
   check(sym.is_valid(), "invalid symbol name");
   check(maximum_supply.is_valid(), "invalid supply");
   check(maximum_supply.amount > 0, "max-supply must be positive");

   stats statstable(get_self(), sym.code().raw());
   auto existing = statstable.find(sym.code().raw());
   check(existing == statstable.end(), "token with symbol already exists");

   statstable.emplace(get_self(), [&](auto &s) {
      s.supply.symbol = maximum_supply.symbol;
      s.max_supply = maximum_supply;
      s.issuer = issuer;
   });

   stats2 statstable2(get_self(), sym.code().raw());
   statstable2.emplace(get_self(), [&](auto &s) {
      s.total_principal.symbol = maximum_supply.symbol;
      s.total_token = 0;
   });
}

void token::issue(const name &to, const asset &quantity, const string &memo) {
   auto sym = quantity.symbol;
   check(sym.is_valid(), "invalid symbol name");
   check(memo.size() <= 256, "memo has more than 256 bytes");

   stats statstable(_self, sym.code().raw());
   auto existing = statstable.find(sym.code().raw());
   check(existing != statstable.end(), "token with symbol does not exist, create token before issue");
   const auto &st = *existing;

   require_auth(st.issuer);
   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must issue positive quantity");

   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
   check(quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

   statstable.modify(st, same_payer, [&](auto &s) {
      s.supply += quantity;
   });

   add_balance(st.issuer, quantity, st.issuer);

   if (to != st.issuer) {
      SEND_INLINE_ACTION(*this, transfer, {{st.issuer, "active"_n}}, {st.issuer, to, quantity, memo});
   }
}

void token::retire(const asset &quantity, const string &memo) {
   auto sym = quantity.symbol;
   check(sym.is_valid(), "invalid symbol name");
   check(memo.size() <= 256, "memo has more than 256 bytes");

   stats statstable(get_self(), sym.code().raw());
   auto existing = statstable.find(sym.code().raw());
   check(existing != statstable.end(), "token with symbol does not exist");
   const auto &st = *existing;

   require_auth(st.issuer);
   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must retire positive quantity");

   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");

   statstable.modify(st, same_payer, [&](auto &s) {
      s.supply -= quantity;
   });

   sub_balance(st.issuer, quantity);
}

void token::transfer(const name &from, const name &to, const asset &quantity, const string &memo) {
   check(from != to, "cannot transfer to self");
   require_auth(from);
   check(is_account(to), "to account does not exist");
   auto sym = quantity.symbol.code();
   stats statstable(get_self(), sym.raw());
   const auto &st = statstable.get(sym.raw());

   if (from != AIRDROP_ACCOUNT) {
      require_recipient(from);
      require_recipient(to);
   }

   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must transfer positive quantity");
   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
   check(memo.size() <= 256, "memo has more than 256 bytes");

   auto payer = has_auth(to) ? to : from;

   // 10% tax
   auto tax = quantity / 10;
   check(tax.amount > 10, "transfer amount too small");
   if (is_whitelist(from)) {
      tax.amount = 0;
   }

   sub_balance(from, quantity + tax);
   add_balance(to, quantity, payer);

   if (tax.amount ==  0) {
      return;
   }

   // market 5%
   auto market_qty = tax / 2;
   auto market_balance = add_balance(MARKET_ACCOUNT, market_qty, _self);

   // reward 3%, save to token contract
   auto reward_qty = tax * 3 / 10;
   add_balance(_self, reward_qty, _self);

   stats2 statstable2(get_self(), sym.raw());
   auto st2 = statstable2.require_find(sym.raw(), "stat2 not found");
   statstable2.modify(st2, same_payer, [&](auto &s) {
      s.total_principal += reward_qty;
   });

   // retire 2%
   auto retire_qty = tax - market_qty - reward_qty;
   statstable.modify(st, same_payer, [&](auto &s) {
      s.supply -= retire_qty;
      s.max_supply -= retire_qty;
   });

   auto sell_qty = market_balance / 2;
   auto eos_value = get_eos_value(sell_qty);
   if (eos_value >= 5000L) {
      auto data = std::make_tuple(MARKET_ACCOUNT, name("swap.defi"), sell_qty, string("swap,0,") + std::to_string(PAIR_ID));
      action(permission_level{MARKET_ACCOUNT, "active"_n}, _self, "transfer"_n, data).send();

      action(permission_level{_self, "active"_n}, _self, "addliquidity"_n, std::make_tuple()).send();
   }
}

void token::stake(const name &owner, const asset &quantity) {
   require_auth(owner);

   check(quantity.amount > 0, "must stake positive quantity");

   // todo check quantity

   auto sym = quantity.symbol.code();
   stats2 statstable2(get_self(), sym.raw());
   auto st2 = statstable2.require_find(sym.raw(), "stat2 not found");

   accounts acnts(get_self(), owner.value);
   auto bi = acnts.require_find(quantity.symbol.code().raw(), "no balance object found");

   stakes stakestable(get_self(), sym.raw());
   auto sti = stakestable.find(owner.value);
   uint64_t staked = sti == stakestable.end() ? 0 : sti->principal.amount;
   check(bi->balance.amount - staked >= quantity.amount, "insufficient balance");

   uint128_t token = 0;
   if (st2->total_token == 0) {
      token = uint128_t(st2->total_principal.amount + quantity.amount) * 100;
   } else {
      uint128_t BASE = uint128_t(10000000) * 10000000;
      uint128_t rate = st2->total_token * BASE / st2->total_principal.amount;
      token = uint128_t(quantity.amount) * rate / BASE;
   }

   statstable2.modify(st2, same_payer, [&](auto &s) {
      s.total_principal += quantity;
      s.total_token += token;
   });


   if (sti == stakestable.end()) {
      stakestable.emplace(owner, [&](auto &a) {
         a.owner = owner;
         a.principal = quantity;
         a.token = token;
      });
   } else {
      stakestable.modify(sti, same_payer, [&](auto &a) {
         a.principal += quantity;
         a.token += token;
      });
   }
}

void token::unstake(const name &owner, const symbol_code sym, uint128_t token) {
   require_auth(owner);

   check(token > 0, "must unstake positive amount");

   _claimunstake(owner, sym, token);
}


void token::claim(const name &owner, const symbol_code sym) {
   require_auth(owner);
   _claimunstake(owner, sym, 0);
}

void token::_claimunstake(const name &owner, const symbol_code sym, uint128_t token) {
   
   stats2 statstable2(get_self(), sym.raw());
   auto st2 = statstable2.require_find(sym.raw(), "stat2 not found");

   stakes stakestable(get_self(), sym.raw());
   auto sti = stakestable.require_find(owner.value, "owner not found");

   // claim
   uint128_t BASE = uint128_t(10000000) * 10000000;
   uint128_t rate = st2->total_principal.amount * BASE / st2->total_token;
   auto principal_now = sti->token * rate / BASE;

   if (principal_now > sti->principal.amount) {
      auto principal_inc = asset(principal_now - sti->principal.amount, sti->principal.symbol);
      stakestable.modify(sti, same_payer, [&](auto &a) {
         a.principal += principal_inc;
      });
      // transfer
      //check(false, "transfer: " + principal_inc.to_string());
      auto data = std::make_tuple(_self, owner, principal_inc, string("claim reward"));
      action(permission_level{_self, "active"_n}, _self, "transfer"_n, data).send();
   }

   // unstake 
   if (token == 0) {
      return;
   }
   check(token <= sti->token, "insufficient token");
   auto principal_amount = token * rate / BASE;
   if (token == sti->token) {
      principal_amount = sti->principal.amount;
   }
   statstable2.modify(st2, same_payer, [&](auto &s) {
      s.total_principal.amount -= principal_amount;
      s.total_token -= token;
      if (s.total_principal.amount < 0) {
         s.total_principal.amount = 0;
      }
   });
   if (token == sti->token) {
      stakestable.erase(sti);
   } else {
      stakestable.modify(sti, same_payer, [&](auto &a) {
         a.principal.amount -= principal_amount;
         a.token -= token;
         if (a.principal.amount < 0) {
            a.principal.amount = 0;
         }
      });
   }
}

void token::addliquidity() {
   require_auth(_self);
   auto em_qty = get_balance(_self, MARKET_ACCOUNT, symbol_code("EMOON"));
   auto data1 = std::make_tuple(MARKET_ACCOUNT, name("swap.defi"), em_qty, string("deposit,") + std::to_string(PAIR_ID));
   action(permission_level{MARKET_ACCOUNT, "active"_n}, _self, "transfer"_n, data1).send();

   auto eos_qty = get_balance(name("eosio.token"), MARKET_ACCOUNT, symbol_code("EOS"));
   auto data2 = std::make_tuple(MARKET_ACCOUNT, name("swap.defi"), eos_qty, string("deposit,") + std::to_string(PAIR_ID));
   action(permission_level{MARKET_ACCOUNT, "active"_n}, name("eosio.token"), "transfer"_n, data2).send();

   auto data3 = std::make_tuple(MARKET_ACCOUNT, PAIR_ID);
   action(permission_level{MARKET_ACCOUNT, "active"_n}, name("swap.defi"), "deposit"_n, data3).send();
}

void token::sub_balance(const name &owner, const asset &value) {
   accounts from_acnts(get_self(), owner.value);

   const auto &from = from_acnts.get(value.symbol.code().raw(), "no balance object found");

   auto balance = from.balance.amount;
   check(balance >= value.amount, "overdrawn balance");

   stakes stakestable(get_self(), value.symbol.code().raw());
   auto sti = stakestable.find(owner.value);
   if (sti != stakestable.end()) {
      check(from.balance - value > sti->principal, "overdrawn balance(staked)");
   }
  
   from_acnts.modify(from, owner, [&](auto &a) {
      a.balance -= value;
   });
}

const asset& token::add_balance(const name &owner, const asset &value, const name &ram_payer) {
   accounts to_acnts(get_self(), owner.value);
   auto to = to_acnts.find(value.symbol.code().raw());
   if (to == to_acnts.end()) {
      to = to_acnts.emplace(ram_payer, [&](auto &a) {
         a.balance = value;
      });
   } else {
      to_acnts.modify(to, same_payer, [&](auto &a) {
         a.balance += value;
      });
   }

   // 100 billion limit in the first hour
   if (current_time_point().sec_since_epoch() < EPOCH_TIME + 3600 && !is_whitelist(owner)) {
      check(to->balance.amount <= 100000000000LL * 100, "each account cannot have more than 100 billion coins in the first hour");
   } 
   return to->balance;
}

bool token::is_whitelist(const name &account) {
   if (account == _self || account == name("swap.defi") || account == name("defisswapcnt") || account == name("newdexpublic") || account == MARKET_ACCOUNT || account == AIRDROP_ACCOUNT) {
      return true;
   }
   return false;
}

void token::open(const name &owner, const symbol &symbol, const name &ram_payer) {
   require_auth(ram_payer);

   check(is_account(owner), "owner account does not exist");

   auto sym_code_raw = symbol.code().raw();
   stats statstable(get_self(), sym_code_raw);
   const auto &st = statstable.get(sym_code_raw, "symbol does not exist");
   check(st.supply.symbol == symbol, "symbol precision mismatch");

   accounts acnts(get_self(), owner.value);
   auto it = acnts.find(sym_code_raw);
   if (it == acnts.end()) {
      acnts.emplace(ram_payer, [&](auto &a) {
         a.balance = asset{0, symbol};
      });
   }
}

void token::close(const name &owner, const symbol &symbol) {
   require_auth(owner);
   accounts acnts(get_self(), owner.value);
   auto it = acnts.find(symbol.code().raw());
   check(it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect.");
   check(it->balance.amount == 0, "Cannot close because the balance is not zero.");
   acnts.erase(it);
}