// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXREBROADCAST_H
#define BITCOIN_TXREBROADCAST_H

#include <policy/feerate.h>
#include <txmempool.h>
#include <validation.h>
#include <net.h>

struct TxIds {
    TxIds(uint256 txid, uint256 wtxid) : m_txid(txid), m_wtxid(wtxid) {}

    const uint256 m_txid;
    const uint256 m_wtxid;
};

struct RebroadcastCounter {
    RebroadcastCounter(NodeId peer_id) {
        setInvSend_peers.push_back(peer_id);
    }

    std::vector<NodeId> setInvSend_peers;
    std::vector<NodeId> inv_peers;
    std::vector<NodeId> getdata_peers;
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

    /** Test only */
    void UpdateAttempt(const uint256& wtxid, const int count, const std::chrono::microseconds last_attempt_time);

    /** Test only */
    bool CheckRecordedAttempt(const uint256& wtxid, const int expected_count, const std::chrono::microseconds expected_timestamp) const;

    /** Test only */
    void UpdateCachedFeeRate(const CFeeRate& new_fee_rate);

private:
    const CTxMemPool& m_mempool;
    const ChainstateManager& m_chainman;
    const CChainParams& m_chainparams;

    /** Block at time of cache */
    CBlockIndex* m_tip_at_cache_time{nullptr};

    /** Minimum fee rate for package to be included in block */
    CFeeRate m_cached_fee_rate;

    /** Keep track of previous rebroadcast attempts.
     *
     *  There are circumstances where our mempool might know about transactions
     *  that will never be mined. Two examples:
     *  1. A software upgrade tightens policy, but the node has not been
     *  upgraded and thus is accepting transactions that other nodes on the
     *  network now reject.
     *  2. An attacker targets the network by sending conflicting transactions
     *  to nodes based on their distance from a miner.
     *
     *  Under such circumstances, we want to avoid wasting a significant amount
     *  of network bandwidth. Also we want to let transactions genuinely expire
     *  from the majority of mempools, unless the source wallet decides to
     *  rebroadcast the transaction.
     *
     *  So, we use this tracker to limit the frequency and the maximum number
     *  of times we will attempt to rebroadcast a transaction.
     * */
    std::unique_ptr<indexed_rebroadcast_set> m_attempt_tracker;

    /** Limit the size of m_attempt_tracker by deleting the oldest entries */
    void TrimMaxRebroadcast();
};

#endif // BITCOIN_TXREBROADCAST_H
