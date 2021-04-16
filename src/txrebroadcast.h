// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXREBROADCAST_H
#define BITCOIN_TXREBROADCAST_H

#include <policy/feerate.h>
#include <txmempool.h>
#include <validation.h>

struct TxIds {
    TxIds(uint256 txid, uint256 wtxid) : m_txid(txid), m_wtxid(wtxid) {}

    const uint256 m_txid;
    const uint256 m_wtxid;
};

class indexed_rebroadcast_set;

class TxRebroadcastHandler
{
public:
    TxRebroadcastHandler(const CTxMemPool& mempool, const ChainstateManager& chainman, const CChainParams& chainparams);
    ~TxRebroadcastHandler();

    TxRebroadcastHandler(const TxRebroadcastHandler& other) = delete;
    TxRebroadcastHandler& operator=(const TxRebroadcastHandler& other) = delete;

    std::vector<TxIds> GetRebroadcastTransactions();

    /** Assemble a block from the highest fee rate packages in the local
     *  mempool. Update the cache with the minimum fee rate for a package to be
     *  included.
     * */
    void CacheMinRebroadcastFee();

    /** Remove transaction entry from the attempt tracker.*/
    void RemoveFromAttemptTracker(const CTransactionRef& tx);

private:
    const CTxMemPool& m_mempool;
    const ChainstateManager& m_chainman;
    const CChainParams& m_chainparams;

    /** Block at time of cache */
    CBlockIndex* m_tip_at_cache_time{nullptr};

    /** Minimum fee rate for package to be included in block */
    CFeeRate m_cached_fee_rate;

    /** Keep track of previous rebroadcast attempts */
    std::unique_ptr<indexed_rebroadcast_set> m_attempt_tracker;

    /** Limit the size of m_attempt_tracker by deleting the oldest entries */
    void TrimMaxRebroadcast();
};

#endif // BITCOIN_TXREBROADCAST_H
