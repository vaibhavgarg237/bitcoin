// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXREBROADCAST_H
#define BITCOIN_TXREBROADCAST_H

#include <policy/feerate.h>
#include <tuple>
#include <txmempool.h>
#include <util/hasher.h>
#include <util/time.h>
#include <validation.h>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

struct TxIds {
    TxIds(uint256 txid, uint256 wtxid) : m_txid(txid), m_wtxid(wtxid) {}

    const uint256 m_txid;
    const uint256 m_wtxid;
};

struct RebroadcastEntry {
    RebroadcastEntry(std::chrono::microseconds now_time, uint256 wtxid)
        : m_last_attempt(now_time),
          m_wtxid(wtxid),
          m_count(1) {}

    std::chrono::microseconds m_last_attempt;
    const uint256 m_wtxid;
    int m_count;
};

/** Used for multi_index tag  */
struct index_by_last_attempt {};

using indexed_rebroadcast_set = boost::multi_index_container<
    RebroadcastEntry,
    boost::multi_index::indexed_by<
        // sorted by wtxid
        boost::multi_index::hashed_unique<
            boost::multi_index::tag<index_by_wtxid>,
            boost::multi_index::member<RebroadcastEntry, const uint256, &RebroadcastEntry::m_wtxid>,
            SaltedTxidHasher
        >,
        // sorted by last rebroadcast time
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<index_by_last_attempt>,
            boost::multi_index::member<RebroadcastEntry, std::chrono::microseconds, &RebroadcastEntry::m_last_attempt>
        >
    >
>;

class TxRebroadcastHandler
{
public:
    TxRebroadcastHandler(CTxMemPool& mempool, ChainstateManager& chainman)
        : m_mempool(mempool),
          m_chainman(chainman){};

    std::vector<TxIds> GetRebroadcastTransactions();

    /** Assemble a block from the highest fee rate packages in the local
     *  mempool. Update the cache with the minimum fee rate for a package to be
     *  included.
     * */
    void CacheMinRebroadcastFee();

protected:
    /** Minimum fee rate for package to be included in block */
    CFeeRate m_cached_fee_rate;

    /** Keep track of previous rebroadcast attempts */
    indexed_rebroadcast_set m_attempt_tracker;

    /** Update an existing RebroadcastEntry - increment count and update timestamp */
    void RecordAttempt(indexed_rebroadcast_set::index<index_by_wtxid>::type::iterator& entry_it);

private:
    const CTxMemPool& m_mempool;

    const ChainstateManager& m_chainman;

    /** Block at time of cache */
    CBlockIndex* m_tip_at_cache_time{nullptr};

    /** Limit the size of m_attempt_tracker by deleting the oldest entries */
    void TrimMaxRebroadcast();
};

#endif // BITCOIN_TXREBROADCAST_H
