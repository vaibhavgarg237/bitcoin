// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXREBROADCAST_H
#define BITCOIN_TXREBROADCAST_H

#include <policy/feerate.h>
#include <tuple>
#include <txmempool.h>

struct TxIds
{
    TxIds(uint256 txid, uint256 wtxid) : m_txid(txid), m_wtxid(wtxid) {}

    const uint256 m_txid;
    const uint256 m_wtxid;
};

class TxRebroadcastHandler
{
public:
    TxRebroadcastHandler(CTxMemPool& mempool) : m_mempool(mempool) {};

    std::vector<TxIds> GetRebroadcastTransactions();

    /** Assemble a block from the highest fee rate packages in the local
     *  mempool. Update the cache with the minimum fee rate for a package to be
     *  included.
     * */
    void CacheMinRebroadcastFee();

private:
    const CTxMemPool& m_mempool;

    /** Block at time of cache */
    CBlockIndex* m_tip_at_cache_time;

    /** Minimum fee rate for package to be included in block */
    CFeeRate m_cached_fee_rate;
};

#endif // BITCOIN_TXREBROADCAST_H
