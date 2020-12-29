// Copyright (c) 2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXREBROADCAST_H
#define BITCOIN_TXREBROADCAST_H

#include <txmempool.h>
#include <uint256.h>
#include <util/time.h>

/** Average delay between rebroadcasts */
static constexpr auto TX_REBROADCAST_INTERVAL = 1h;

/** Frequency of updating the fee rate cache */
static constexpr auto REBROADCAST_FEE_RATE_CACHE_INTERVAL = 20min;

class TxRebroadcastCalculator
{
public:
    TxRebroadcastCalculator(CTxMemPool& mempool) : m_mempool(mempool) {};

    /** Timer for updating fee rate cache */
    std::chrono::seconds m_next_min_fee_cache{0};

    /** Block at time of cache */
    CBlockIndex* m_tip_at_cache_time;

    /** Minimum package fee rate for block inclusion */
    CFeeRate m_cached_fee_rate;

    /** Identify rebroadcast candidates. We select the highest fee-rate
     *  transactions in the mempool by using CreateNewBlock with specific
     *  rebroadcast parameters. Then pass candidates through a fee rate filter
     *  by comparing their package fee rates to the cached fee rate and return
     *  the remaining set.
     *
     *  @param wtxid Whether the set should return txids or wtxids.
     */
    std::vector<uint256> GetRebroadcastTransactions(bool is_wtxid);

    /** Assemble a block from the highest fee rate packages from the local
     *  mempool. Update the cache with the minimum fee rate for a package to be
     *  included.
     */
    void CacheMinRebroadcastFee();

private:
    CTxMemPool& m_mempool;
};

#endif // BITCOIN_TXREBROADCAST_H
