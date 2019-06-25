#include "hdddeposit.hpp"
#include <eosiolib/action.hpp>
#include <eosiolib/chain.h>
#include <eosiolib/symbol.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/serialize.hpp>
#include <eosiolib/multi_index.hpp>
#include <eosio.token/eosio.token.hpp>

using namespace eosio;

static constexpr eosio::name active_permission{N(active)};
static constexpr eosio::name token_account{N(eosio.token)};
static constexpr eosio::name system_account{N(eosio)};
static constexpr eosio::name hdd_deposit_account{N(hdddeposit12)};

void hdddeposit::paydeposit(account_name user, uint64_t minerid, asset quant) {
    
    require_auth(user);

    eosio_assert(is_account(user), "user is not an account.");
    eosio_assert(quant.symbol == CORE_SYMBOL, "must use core asset for hdd deposit.");

    //check if user has enough YTA balance for deposit
    auto balance   = eosio::token(N(eosio.token)).get_balance( user , quant.symbol.name() );
    asset real_balance = balance;
    accdeposit_table _deposit(_self, user);
    auto acc = _deposit.find( user );    
    if ( acc != _deposit.end() ) {
        real_balance.amount -= acc->deposit.amount;
        real_balance.amount -= acc->forfeit.amount;     
    }
    //to do : also need sub lock_token in futuer
    //......

    eosio_assert( real_balance.amount >= quant.amount, "user balance not enough." );

    //insert or update minerdeposit table
    minerdeposit_table _mdeposit(_self, _self);
    auto miner = _mdeposit.find( minerid );
    if ( miner == _mdeposit.end() ) {
        _mdeposit.emplace( _self, [&]( auto& a ){
            a.minerid = minerid;
            a.account_name = name{user};
            a.deposit = quant;
            a.dep_total = quant;
        });
    } else {
        _mdeposit.modify( miner, 0, [&]( auto& a ) {
            eosio_assert(a.account_name == user, "must use same account to increase deposit.");
            a.deposit += quant;
            a.dep_total += quant;
        });
    }

    //insert or update accdeposit table
    if ( acc == _deposit.end() ) {
        _deposit.emplace( _self, [&]( auto& a ){
            a.account_name = name{user};
            a.deposit = quant;
        });
    } else {
        _deposit.modify( acc, 0, [&]( auto& a ) {
            a.deposit += quant;
        });
    }
}

void hdddeposit::undeposit(name user, uint64_t minerid, asset quant) {

    require_auth(_self); // need hdd official account to sign this action.

    eosio_assert(is_account(user), "user is not an account.");
    eosio_assert(quant.symbol == CORE_SYMBOL, "must use core asset for hdd deposit.");

    minerdeposit_table _mdeposit(_self, _self);
    accdeposit_table   _deposit(_self, user.value);
    const auto& miner = _mdeposit.get( minerid, "no deposit record for this minerid.");
    eosio_assert( miner.deposit.amount >= quant.amount, "overdrawn deposit." );
    eosio_assert(miner.account_name == user, "must use same account to decrease deposit.");
    const auto& acc = _deposit.get( user.value, "no deposit record for this user.");
    eosio_assert( acc.deposit.amount >= quant.amount, "overdrawn deposit." );

    _mdeposit.modify( miner, 0, [&]( auto& a ) {
        a.deposit.amount -= quant.amount;
    });

    _deposit.modify( acc, 0, [&]( auto& a ) {
        a.deposit.amount -= quant.amount;
    });
}

void hdddeposit::payforfeit(name user, uint64_t minerid, asset quant, uint8_t acc_type, name caller) {

    if(acc_type == 2) {
        eosio_assert(is_account(caller), "caller not a account.");
        eosio_assert(is_bp_account(caller.value), "caller not a BP account.");
        require_auth( caller );
    } else {
        require_auth( _self );
    }

    eosio_assert(is_account(user), "user is not an account.");
    eosio_assert(quant.symbol == CORE_SYMBOL, "must use core asset for hdd deposit.");

    minerdeposit_table _mdeposit(_self, _self);
    accdeposit_table   _deposit(_self, user.value);
    const auto& miner = _mdeposit.get( minerid, "no deposit record for this minerid.");
    eosio_assert( miner.deposit.amount >= quant.amount, "overdrawn deposit." );
    eosio_assert(miner.account_name == user, "must use same account to pay forfeit.");
    const auto& acc = _deposit.get( user.value, "no deposit record for this user.");
    eosio_assert( acc.deposit.amount >= quant.amount, "overdrawn deposit." );

    _mdeposit.modify( miner, 0, [&]( auto& a ) {
        a.deposit.amount -= quant.amount;
    });

    _deposit.modify( acc, 0, [&]( auto& a ) {
        a.deposit.amount -= quant.amount;
        a.forfeit.amount += quant.amount;
    });    

}

void hdddeposit::drawforfeit(name user, uint8_t acc_type, name caller) {

    if(acc_type == 2) {
        eosio_assert(is_account(caller), "caller not a account.");
        eosio_assert(is_bp_account(caller.value), "caller not a BP account.");
        require_auth( caller );
    } else {
        require_auth( _self );
    }

    eosio_assert(is_account(user), "user is not an account.");
    accdeposit_table   _deposit(_self, user.value);
    const auto& acc = _deposit.get( user.value, "no deposit record for this user.");

    asset quant{acc.forfeit.amount, CORE_SYMBOL};
    action(
       permission_level{user, active_permission},
       token_account, N(transfer),
       std::make_tuple(user, hdd_deposit_account, quant, std::string("draw forfeit")))
       .send();
    
    _deposit.modify( acc, 0, [&]( auto& a ) {
        a.forfeit.amount = 0;
    });      

}

void hdddeposit::cutvote(name user, uint8_t acc_type, name caller) {

    if(acc_type == 2) {
        eosio_assert(is_account(caller), "caller not a account.");
        eosio_assert(is_bp_account(caller.value), "caller not a BP account.");
        require_auth( caller );
    } else {
        require_auth( _self );
    }

    eosio_assert(is_account(user), "user is not an account.");
    accdeposit_table   _deposit(_self, user.value);
    const auto& acc = _deposit.get( user.value, "no deposit record for this user.");

    asset quantb{acc.forfeit.amount/2, CORE_SYMBOL};
    asset quantw{acc.forfeit.amount/2, CORE_SYMBOL};
    
    //asset quantb{10000, CORE_SYMBOL};
    //asset quantw{10000, CORE_SYMBOL};

    action(
       permission_level{user, active_permission},
       system_account, N(undelegatebw),
       std::make_tuple(user, user, quantb, quantw))
       .send();
}

void hdddeposit::clearminer(uint64_t minerid) {
    require_auth(_self);
    /* 
    minerdeposit_table _mdeposit(_self, _self);

    _mdeposit.emplace( _self, [&]( auto& a ){
        a.minerid = 100001;
        a.account_name = name{N(usernamefang)};
        a.deposit = asset{0, CORE_SYMBOL};
    });

    _mdeposit.emplace( _self, [&]( auto& a ){
        a.minerid = 100002;
        a.account_name = name{N(usernamexiao)};
        a.deposit = asset{0, CORE_SYMBOL};
    });    


    _mdeposit.emplace( _self, [&]( auto& a ){
        a.minerid = 100003;
        a.account_name = name{N(usernamefang)};
        a.deposit = asset{0, CORE_SYMBOL};
    });    

    _mdeposit.emplace( _self, [&]( auto& a ){
        a.minerid = 100004;
        a.account_name = name{N(usernamexiao)};
        a.deposit = asset{0, CORE_SYMBOL};
    });   
    */  
    
    minerdeposit_table _mdeposit(_self, _self);
    const auto& miner = _mdeposit.get( minerid, "no deposit record for this minerid.");
     _mdeposit.erase( miner );
    
}

void hdddeposit::clearacc(name user) {
    require_auth(_self); 
    accdeposit_table   _deposit(_self, user.value);
    const auto& acc = _deposit.get( user.value, "no deposit record for this user.");
    _deposit.erase( acc );
}

void hdddeposit::setrate(int64_t rate) {
    require_auth(_self);
    grate_singleton _rate(_self, _self);
    deposit_rate    _rateState;

   if (_rate.exists())
      _rateState = _rate.get();
   else
      _rateState = deposit_rate{};

    _rateState.rate = rate;

    _rate.set(_rateState, _self);

}

bool hdddeposit::is_bp_account(uint64_t uservalue)
{
   account_name producers[21];
   uint32_t bytes_populated = get_active_producers(producers, sizeof(account_name) * 21);
   uint32_t count = bytes_populated / sizeof(account_name);
   for (uint32_t i = 0; i < count; i++)
   {
      if (producers[i] == uservalue)
         return true;
   }
   return false;
}


EOSIO_ABI( hdddeposit, (paydeposit)(undeposit)(payforfeit)(drawforfeit)(cutvote)(clearminer)(clearacc)(setrate))
