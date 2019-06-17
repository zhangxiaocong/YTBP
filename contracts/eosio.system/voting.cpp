/**
 *  @file
 *  @copyright defined in eos/LICENSE
 */
#include "eosio.system.hpp"

#include <eosiolib/eosio.hpp>
#include <eosiolib/crypto.h>
#include <eosiolib/print.hpp>
#include <eosiolib/datastream.hpp>
#include <eosiolib/serialize.hpp>
#include <eosiolib/multi_index.hpp>
#include <eosiolib/privileged.hpp>
#include <eosiolib/singleton.hpp>
#include <eosiolib/transaction.hpp>
#include <eosio.token/eosio.token.hpp>

#include <algorithm>
#include <cmath>

namespace eosiosystem {
   using eosio::indexed_by;
   using eosio::const_mem_fun;
   using eosio::bytes;
   using eosio::print;
   using eosio::singleton;
   using eosio::transaction;

   /**
    *  This method will create a producer_config and producer_info object for 'producer'
    *
    *  @pre producer is not already registered
    *  @pre producer to register is an account
    *  @pre authority of producer to register
    *
    */
   void system_contract::regproducer( const account_name producer, const eosio::public_key& producer_key, const std::string& url, uint16_t location ) {
      eosio_assert( url.size() < 512, "url too long" );
      eosio_assert( producer_key != eosio::public_key(), "public key should not be the default value" );
      require_auth( producer );

      auto prod = _producers.find( producer );

      if ( prod != _producers.end() ) {
         _producers.modify( prod, producer, [&]( producer_info& info ){
               info.producer_key = producer_key;
               info.is_active    = true;
               info.url          = url;
               info.location     = location;
            });
         
         active_producer_seq(producer, producer_key, true);
      } else {
         _producers.emplace( producer, [&]( producer_info& info ){
               info.owner         = producer;
               info.total_votes   = 0;
               info.producer_key  = producer_key;
               info.is_active     = true;
               info.url           = url;
               info.location      = location;
         });
      }
   }

   void system_contract::unregprod( const account_name producer ) {
      require_auth( producer );

      const auto& prod = _producers.get( producer, "producer not found" );

      _producers.modify( prod, 0, [&]( producer_info& info ){
            info.deactivate();
      });

      active_producer_seq(producer, public_key(), false);
   }
//##YTA-Change  start: 
   void system_contract::clsprods2() {
      require_auth( _self );

      while (_producers2.begin() != _producers2.end())
         _producers2.erase(_producers2.begin()); 

      for( uint16_t seq = 1; seq <= 21; seq++ ) {
         producers_seq_table _prod_seq( _self, seq );
         if( _prod_seq.begin() != _prod_seq.end() )
            _prod_seq.erase(_prod_seq.begin()); 
      }      
   }

   void system_contract::seqproducer( const account_name producer, uint16_t seq , uint8_t level ) {
      require_auth( _self );
      
      eosio_assert(seq >= 1 && seq <= 21 , "invalidate seq number");
      eosio_assert(level >= 1 && level <= 3 , "invalidate level number");
      //const auto& prod = _producers.get( producer, "producer not found" );
      
      auto it = _producers2.find(producer);
      if (it == _producers2.end()) {
         _producers2.emplace(_self, [&](auto &row) {
            row.owner = producer;
            row.seq_num = seq;
         });
         add_producer_seq(producer, seq, level);
      } else {
         uint16_t old_seq = it->seq_num;   
         _producers2.modify(it, _self, [&](auto &row) {
            row.seq_num = seq;
         });
         rm_producer_seq(producer, old_seq);
         add_producer_seq(producer, seq, level);
      }
   }

   void system_contract::rm_producer_seq(const account_name producer, uint16_t seq) {
      producers_seq_table _prodseq(_self, seq);
      auto ps_itr = _prodseq.find (seq); 
      if( ps_itr == _prodseq.end() ) 
         return;
      _prodseq.modify( ps_itr, _self, [&]( producers_seq& info ){
         if(info.prods_l1.owner == producer) {
            info.prods_l1.owner = 0;
            info.prods_l1.producer_key = public_key();
            info.prods_l1.all_stake = 0;
            info.prods_l1.total_votes = 0;            
            info.prods_l1.is_active = false;
         }

         for( auto it2 =  info.prods_l2.begin(); it2 !=  info.prods_l2.end(); it2++ ) {
            if(it2->owner == producer) {
               info.prods_l2.erase(it2);
               break;
            } 
         }

         for( auto it3 =  info.prods_l3.begin(); it3 !=  info.prods_l3.end(); it3++ ) {
            if(it3->owner == producer) {
               info.prods_l3.erase(it3);
               break;
            } 
         }   

         for( auto itall =  info.prods_all.begin(); itall !=  info.prods_all.end(); itall++ ) {
            if(itall->owner == producer) {
               info.prods_all.erase(itall);
               break;
            } 
         }   
      });
   }

   void system_contract::add_producer_seq( const account_name producer, uint16_t seq , uint8_t level ) {
      producers_seq_table _prodseq(_self, seq);
      prod_meta prodm;
      //need retrive from system producers table
      const auto& prod = _producers.get( producer, "producer not found" );
      prodm.owner = producer;
      prodm.producer_key = prod.producer_key;
      prodm.all_stake = 0;
      prodm.total_votes = prod.total_votes;
      prodm.is_active = prod.is_active;
      auto ps_itr = _prodseq.find (seq);
      if( ps_itr == _prodseq.end() ) {
         _prodseq.emplace(_self, [&](auto &row) {
            row.seq_num = seq;
            row.prods_all.push_back(prodm);
            if(level == 1) {
               row.prods_l1 = prodm;
            } else if(level == 2) {
               row.prods_l2.push_back(prodm);
            } else if(level == 3) {
               row.prods_l3.push_back(prodm);
            }
         });
      } else {
         _prodseq.modify(ps_itr, _self, [&](auto &row) {
            row.prods_all.push_back(prodm);
            if(level == 1) {
               row.prods_l1 = prodm;
            } else if(level == 2) {
               row.prods_l2.push_back(prodm);
            } else if(level == 3) {
               row.prods_l3.push_back(prodm);
            }

         });
      }       
   }

   void system_contract::active_producer_seq( const account_name producer, const eosio::public_key& producer_key, bool isactive) {
      auto it = _producers2.find(producer);

      if (it == _producers2.end()) 
         return;
      
      uint16_t seq = it->seq_num;
      producers_seq_table _prodseq(_self, seq);
      auto ps_itr = _prodseq.find (seq);
      if( ps_itr == _prodseq.end() )
         return;

      _prodseq.modify( ps_itr, _self, [&]( producers_seq& info ){
         if(info.prods_l1.owner == producer) {
            info.prods_l1.is_active = isactive;
            info.prods_l1.producer_key = producer_key;
         }

         for( auto it2 =  info.prods_l2.begin(); it2 !=  info.prods_l2.end(); it2++ ) {
            if(it2->owner == producer) {
               it2->is_active = isactive;
               it2->producer_key = producer_key;
               break;
            } 
         }

         for( auto it3 =  info.prods_l3.begin(); it3 !=  info.prods_l3.end(); it3++ ) {
            if(it3->owner == producer) {
               it3->is_active = isactive;
               it3->producer_key = producer_key;
               break;
            } 
         }   

         for( auto itall =  info.prods_all.begin(); itall !=  info.prods_all.end(); itall++ ) {
            if(itall->owner == producer) {
               itall->is_active = isactive;
               itall->producer_key = producer_key;
               break;
            } 
         }   
      });
      
   }

   void system_contract::update_producers_seq_totalvotes( uint16_t seq_num, account_name owner, double total_votes) {
      
      producers_seq_table _prodseq(_self, seq_num);
      auto ps_itr = _prodseq.find (seq_num);
      
      if( ps_itr == _prodseq.end() )
         return;      

      _prodseq.modify( ps_itr, _self, [&]( producers_seq& info ){
         if( info.prods_l1.owner == owner ) {
            info.prods_l1.total_votes = total_votes;
            //return;
         }

         for(auto it = info.prods_l2.begin(); it != info.prods_l2.end(); it++) {
            if(it->owner == owner) {
               it->total_votes = total_votes;
               break;
            }
         }

         for(auto it = info.prods_l3.begin(); it != info.prods_l3.end(); it++) {
            if(it->owner == owner) {
               it->total_votes = total_votes;
               break;
            }
         }

         for(auto it = info.prods_all.begin(); it != info.prods_all.end(); it++) {
            if(it->owner == owner) {
               it->total_votes = total_votes;
               break;
            }
         }         

      });
   }

   void system_contract::update_elected_producers_yta( block_timestamp block_time ) {
      _gstate.last_producer_schedule_update = block_time;

   }   
//##YTA-Change  end:  

   void system_contract::update_elected_producers( block_timestamp block_time ) {
      _gstate.last_producer_schedule_update = block_time;

      auto idx = _producers.get_index<N(prototalvote)>();

      std::vector< std::pair<eosio::producer_key,uint16_t> > top_producers;
      top_producers.reserve(21);

      for ( auto it = idx.cbegin(); it != idx.cend() && top_producers.size() < 21 && 0 < it->total_votes && it->active(); ++it ) {
         top_producers.emplace_back( std::pair<eosio::producer_key,uint16_t>({{it->owner, it->producer_key}, it->location}) );
      }

      if ( top_producers.size() < _gstate.last_producer_schedule_size ) {
         return;
      }

      /// sort by producer name
      std::sort( top_producers.begin(), top_producers.end() );

      std::vector<eosio::producer_key> producers;

      producers.reserve(top_producers.size());
      for( const auto& item : top_producers )
         producers.push_back(item.first);

      bytes packed_schedule = pack(producers);

      if( set_proposed_producers( packed_schedule.data(),  packed_schedule.size() ) >= 0 ) {
         _gstate.last_producer_schedule_size = static_cast<decltype(_gstate.last_producer_schedule_size)>( top_producers.size() );
      }
   }

   double stake2vote( int64_t staked ) {
      /// TODO subtract 2080 brings the large numbers closer to this decade
      double weight = int64_t( (now() - (block_timestamp::block_timestamp_epoch / 1000)) / (seconds_per_day * 7) )  / double( 52 );
      return double(staked) * std::pow( 2, weight );
   }
   /**
    *  @pre producers must be sorted from lowest to highest and must be registered and active
    *  @pre if proxy is set then no producers can be voted for
    *  @pre if proxy is set then proxy account must exist and be registered as a proxy
    *  @pre every listed producer or proxy must have been previously registered
    *  @pre voter must authorize this action
    *  @pre voter must have previously staked some EOS for voting
    *  @pre voter->staked must be up to date
    *
    *  @post every producer previously voted for will have vote reduced by previous vote weight
    *  @post every producer newly voted for will have vote increased by new vote amount
    *  @post prior proxy will proxied_vote_weight decremented by previous vote weight
    *  @post new proxy will proxied_vote_weight incremented by new vote weight
    *
    *  If voting for a proxy, the producer votes will not change until the proxy updates their own vote.
    */
   void system_contract::voteproducer( const account_name voter_name, const account_name proxy, const std::vector<account_name>& producers ) {
      require_auth( voter_name );
      update_votes( voter_name, proxy, producers, true );
   }

   void system_contract::update_votes( const account_name voter_name, const account_name proxy, const std::vector<account_name>& producers, bool voting ) {
      //validate input
      if ( proxy ) {
         eosio_assert( producers.size() == 0, "cannot vote for producers and proxy at same time" );
         eosio_assert( voter_name != proxy, "cannot proxy to self" );
         require_recipient( proxy );
      } else {
         //##YTA-Change  start:         
         //eosio_assert( producers.size() <= 30, "attempt to vote for too many producers" );
         // One voter can only vote for one producer
         eosio_assert( producers.size() <= 1, "attempt to vote for too many producers" );
         //##YTA-Change  end:
         for( size_t i = 1; i < producers.size(); ++i ) {
            eosio_assert( producers[i-1] < producers[i], "producer votes must be unique and sorted" );
         }
      }

      auto voter = _voters.find(voter_name);
      eosio_assert( voter != _voters.end(), "user must stake before they can vote" ); /// staking creates voter object
      eosio_assert( !proxy || !voter->is_proxy, "account registered as a proxy is not allowed to use a proxy" );

      /**
       * The first time someone votes we calculate and set last_vote_weight, since they cannot unstake until
       * after total_activated_stake hits threshold, we can use last_vote_weight to determine that this is
       * their first vote and should consider their stake activated.
       */
      if( voter->last_vote_weight <= 0.0 ) {
         _gstate.total_activated_stake += voter->staked;
         if( _gstate.total_activated_stake >= min_activated_stake && _gstate.thresh_activated_stake_time == 0 ) {
            _gstate.thresh_activated_stake_time = current_time();
         }
      }

      auto new_vote_weight = stake2vote( voter->staked );
      if( voter->is_proxy ) {
         new_vote_weight += voter->proxied_vote_weight;
      }

      boost::container::flat_map<account_name, pair<double, bool /*new*/> > producer_deltas;
      if ( voter->last_vote_weight > 0 ) {
         if( voter->proxy ) {
            auto old_proxy = _voters.find( voter->proxy );
            eosio_assert( old_proxy != _voters.end(), "old proxy not found" ); //data corruption
            _voters.modify( old_proxy, 0, [&]( auto& vp ) {
                  vp.proxied_vote_weight -= voter->last_vote_weight;
               });
            propagate_weight_change( *old_proxy );
         } else {
            for( const auto& p : voter->producers ) {
               auto& d = producer_deltas[p];
               d.first -= voter->last_vote_weight;
               d.second = false;
            }
         }
      }

      if( proxy ) {
         auto new_proxy = _voters.find( proxy );
         eosio_assert( new_proxy != _voters.end(), "invalid proxy specified" ); //if ( !voting ) { data corruption } else { wrong vote }
         eosio_assert( !voting || new_proxy->is_proxy, "proxy not found" );
         if ( new_vote_weight >= 0 ) {
            _voters.modify( new_proxy, 0, [&]( auto& vp ) {
                  vp.proxied_vote_weight += new_vote_weight;
               });
            propagate_weight_change( *new_proxy );
         }
      } else {
         if( new_vote_weight >= 0 ) {
            for( const auto& p : producers ) {
               auto& d = producer_deltas[p];
               d.first += new_vote_weight;
               d.second = true;
            }
         }
      }

      for( const auto& pd : producer_deltas ) {
         double total_votes = 0;
         auto pitr = _producers.find( pd.first );
         if( pitr != _producers.end() ) {
            eosio_assert( !voting || pitr->active() || !pd.second.second /* not from new set */, "producer is not currently registered" );
            _producers.modify( pitr, 0, [&]( auto& p ) {
               p.total_votes += pd.second.first;
               if ( p.total_votes < 0 ) { // floating point arithmetics can give small negative numbers
                  p.total_votes = 0;
               }
               _gstate.total_producer_vote_weight += pd.second.first;
               //eosio_assert( p.total_votes >= 0, "something bad happened" );
               total_votes = p.total_votes;
            });
         } else {
            eosio_assert( !pd.second.second /* not from new set */, "producer is not registered" ); //data corruption
         }
         //##YTA-Change  start:
         auto pitr2 = _producers2.find( pd.first );
         if( pitr2 != _producers2.end() ) {
            //pitr2->seq_num   
            update_producers_seq_totalvotes(pitr2->seq_num, pd.first, total_votes); 
         }  else {
            eosio_assert( !pd.second.second /* not from new set */, "producer is not registered" ); //data corruption
         }     
         //##YTA-Change  end:         


      }

      _voters.modify( voter, 0, [&]( auto& av ) {
         av.last_vote_weight = new_vote_weight;
         av.producers = producers;
         av.proxy     = proxy;
      });
   }

   /**
    *  An account marked as a proxy can vote with the weight of other accounts which
    *  have selected it as a proxy. Other accounts must refresh their voteproducer to
    *  update the proxy's weight.
    *
    *  @param isproxy - true if proxy wishes to vote on behalf of others, false otherwise
    *  @pre proxy must have something staked (existing row in voters table)
    *  @pre new state must be different than current state
    */
   void system_contract::regproxy( const account_name proxy, bool isproxy ) {
      require_auth( proxy );

      auto pitr = _voters.find(proxy);
      if ( pitr != _voters.end() ) {
         eosio_assert( isproxy != pitr->is_proxy, "action has no effect" );
         eosio_assert( !isproxy || !pitr->proxy, "account that uses a proxy is not allowed to become a proxy" );
         _voters.modify( pitr, 0, [&]( auto& p ) {
               p.is_proxy = isproxy;
            });
         propagate_weight_change( *pitr );
      } else {
         _voters.emplace( proxy, [&]( auto& p ) {
               p.owner  = proxy;
               p.is_proxy = isproxy;
            });
      }
   }

   void system_contract::propagate_weight_change( const voter_info& voter ) {
      eosio_assert( voter.proxy == 0 || !voter.is_proxy, "account registered as a proxy is not allowed to use a proxy" );
      double new_weight = stake2vote( voter.staked );
      if ( voter.is_proxy ) {
         new_weight += voter.proxied_vote_weight;
      }

      /// don't propagate small changes (1 ~= epsilon)
      if ( fabs( new_weight - voter.last_vote_weight ) > 1 )  {
         if ( voter.proxy ) {
            auto& proxy = _voters.get( voter.proxy, "proxy not found" ); //data corruption
            _voters.modify( proxy, 0, [&]( auto& p ) {
                  p.proxied_vote_weight += new_weight - voter.last_vote_weight;
               }
            );
            propagate_weight_change( proxy );
         } else {
            auto delta = new_weight - voter.last_vote_weight;
            for ( auto acnt : voter.producers ) {
               auto& pitr = _producers.get( acnt, "producer not found" ); //data corruption
               _producers.modify( pitr, 0, [&]( auto& p ) {
                     p.total_votes += delta;
                     _gstate.total_producer_vote_weight += delta;
               });
            }
         }
      }
      _voters.modify( voter, 0, [&]( auto& v ) {
            v.last_vote_weight = new_weight;
         }
      );
   }

} /// namespace eosiosystem
