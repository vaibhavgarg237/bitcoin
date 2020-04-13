// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NET_PROCESSING_H
#define BITCOIN_NET_PROCESSING_H

#include <consensus/params.h>
#include <net.h>
#include <sync.h>
#include <util/transaction.h>
#include <validationinterface.h>

#include <unordered_map>

class CTxMemPool;

extern RecursiveMutex cs_main;
extern RecursiveMutex g_cs_orphans;

/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
static const unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS = 100;
/** Default number of orphan+recently-replaced txn to keep around for block reconstruction */
static const unsigned int DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN = 100;
static const bool DEFAULT_PEERBLOOMFILTERS = false;

class PeerLogicValidation final : public CValidationInterface, public NetEventsInterface {
private:
    CConnman* const connman;
    BanMan* const m_banman;
    CTxMemPool& m_mempool;

    bool CheckIfBanned(CNode* pnode) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

public:
    PeerLogicValidation(CConnman* connman, BanMan* banman, CScheduler& scheduler, CTxMemPool& pool);

    /**
     * Overridden from CValidationInterface.
     */
    void BlockConnected(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex* pindexConnected) override;
    void BlockDisconnected(const std::shared_ptr<const CBlock> &block, const CBlockIndex* pindex) override;
    /**
     * Overridden from CValidationInterface.
     */
    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) override;
    /**
     * Overridden from CValidationInterface.
     */
    void BlockChecked(const CBlock& block, const BlockValidationState& state) override;
    /**
     * Overridden from CValidationInterface.
     */
    void NewPoWValidBlock(const CBlockIndex *pindex, const std::shared_ptr<const CBlock>& pblock) override;

    /** Initialize a peer by adding it to mapNodeState and pushing a message requesting its version */
    void InitializeNode(CNode* pnode) override;
    /** Handle removal of a peer by updating various state and removing it from mapNodeState */
    void FinalizeNode(NodeId nodeid, bool& fUpdateConnectionTime) override;
    /**
    * Process protocol messages received from a given node
    *
    * @param[in]   pfrom           The node which we have received messages from.
    * @param[in]   interrupt       Interrupt condition for processing threads
    */
    bool ProcessMessages(CNode* pfrom, std::atomic<bool>& interrupt) override;
    /**
    * Send queued protocol messages to be sent to a give node.
    *
    * @param[in]   pto             The node which we are sending messages to.
    * @return                      True if there is more work to be done
    */
    bool SendMessages(CNode* pto) override EXCLUSIVE_LOCKS_REQUIRED(pto->cs_sendProcessing);

    /** Consider evicting an outbound peer based on the amount of time they've been behind our tip */
    void ConsiderEviction(CNode *pto, int64_t time_in_seconds) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Evict extra outbound peers. If we think our tip may be stale, connect to an extra outbound */
    void CheckForStaleTipAndEvictPeers(const Consensus::Params &consensusParams);
    /** If we have extra outbound peers, try to disconnect the one with the oldest block announcement */
    void EvictExtraOutboundPeers(int64_t time_in_seconds) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

private:
    int64_t m_stale_tip_check_time; //!< Next time to check for stale tip
};

struct CNodeStateStats {
    int nMisbehavior = 0;
    int nSyncHeight = -1;
    int nCommonHeight = -1;
    std::vector<int> vHeightInFlight;
};

/** Get statistics from node state */
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats);

/** Relay transaction to every node */
void RelayTransaction(const uint256&, const CConnman& connman);

/*
 * A transaction that has been announced to us by a single peer. We store
 * the txid and the request time.
 */
struct AnnouncedTx {
    //! The txid of the announced transaction.
    uint256 m_hash;

    //! A timestamp for this transaction from this peer:
    //  - for transactions which are announced but not yet requested,
    //    this is the next time that we'll consider downloading the
    //    transaction from this peer.
    //  - for AnnouncedTx which we've requested, this is the time that we
    //    will expire the transaction from this peer.
    std::chrono::microseconds m_timestamp;

    AnnouncedTx(uint256 hash, std::chrono::microseconds timestamp) :
        m_hash(hash), m_timestamp(timestamp)  {}
};

//! Compare function for AnnouncedTxs. Sorts first on the request time, and then
//  on txid as a tiebreaker.
struct AnnouncedTxTimeCompare
{
    bool operator()(const std::shared_ptr<AnnouncedTx> lhs, const std::shared_ptr<AnnouncedTx> rhs) const
    {
        return lhs->m_timestamp < rhs->m_timestamp ||
            (lhs->m_timestamp == rhs->m_timestamp && lhs->m_hash < rhs->m_hash);
    }
};

/*
 * State associated with transaction download for a single peer.
 *
 * Tx download design goals:
 *
 * - Request a transaction from one peer at a time to avoid wasting
 *   bandwidth.
 * - Prefer downloading from outbound peers. This makes it more difficult
 *   for adversaries to slow down or prevent tx relay to us, and for spy
 *   nodes to map the topology of the tx relay network.
 * - Limit the number of pending announced transactions and transactions
 *   in flight from any peer.
 * - Timeout transaction download from a peer after a reasonable period
 *   and attempt to download from another peer that has announced
 *   the same transaction. Again, prefer outbound peers.
 *
 * Tx download algorithm:
 *
 *   When an inv is received from a peer, queue the txid along with a
 *   request time, as long as there aren't too many announced transactions
 *   already pending from this peer(MAX_PEER_TX_ANNOUNCEMENTS).
 *
 *   The request time is set to now for outbound peers, and now + 2 seconds
 *   for inbound peers. This is the earliest time we'll consider trying to
 *   request the transaction from that peer in SendMessages(). The delay
 *   for inbound peers is to allow outbound peers a chance to announce
 *   before we request from inbound peers, to prevent an adversary from
 *   using inbound connections to blind us to a transaction (InvBlock).
 *
 *   When we call SendMessages() for a given peer,
 *   we will loop over the transactions in m_tx_process_time, looking
 *   at the transactions whose process_time <= nNow. We'll request each
 *   such transaction that we don't have already and that hasn't been
 *   requested from another peer recently, up until we hit the
 *   MAX_PEER_TX_IN_FLIGHT limit for the peer. Then we'll update
 *   g_already_asked_for for each requested txid, storing the time of the
 *   GETDATA request. We use g_already_asked_for to coordinate transaction
 *   requests amongst our peers.
 *
 *   For transactions that we still need but we have already recently
 *   requested from another peer, we'll reset the request time for this
 *   peer to the point in the future at which the most recent GETDATA
 *   request would time out. We add an additional delay for inbound peers,
 *   again to prefer attempting download from outbound peers first.
 *   We also add an extra small random delay up to 2 seconds
 *   to avoid biasing some peers over others. (e.g., due to fixed ordering
 *   of peer processing in ThreadMessageHandler).
 *
 *   When we receive a transaction from a peer, we remove the txid from
 *   here and from the g_already_asked_for for that entry, so that if
 *   somehow the transaction is not accepted but also not added to the
 *   reject filter, then we will eventually redownload from other peers.
 *
 *   Periodically (every TX_EXPIRY_INTERVAL minutes on average), we'll
 *   clear out any transactions that have been in-flight for more than
 *   TX_EXPIRY_INTERVAL minutes from that peer.
 *
 * Class invariants:
 *
 * - m_txs is bounded by MAX_PEER_TX_ANNOUNCEMENTS
 * - m_requested_txs is bounded by MAX_PEER_TX_IN_FLIGHT
 * - every tx in m_txs is EITHER in m_announced_txs OR m_requested_txs
 * - entries are cleared out from m_announced_txs as current_time advances
 * - entries are cleared out from m_requested_txs when the peer responds
 *   to the request or after an expiry time
 */
class TxDownloadState {
private:
    //! All transactions that have been announced by this peer, ordered by hash
    std::unordered_map<uint256, std::shared_ptr<AnnouncedTx>, SaltedTxidHasher> m_txs;

    //! Transactions that have been announced that we haven't requested from this
    //  peer, ordered by request time
    std::set<std::shared_ptr<AnnouncedTx>, AnnouncedTxTimeCompare> m_announced_txs;

    //! Transactions that we have requested from this peer, ordered by expiry time
    std::set<std::shared_ptr<AnnouncedTx>, AnnouncedTxTimeCompare> m_requested_txs;

public:
    // The peer has sent us an INV. Keep track of the hash and when to
    // request the transaction from this peer.
    void AddAnnouncedTx(uint256 hash, std::chrono::microseconds request_time);

    // We have requested this transaction from another peer. Reset this
    // peer's request time for this transaction to after the outstanding
    // request times out.
    void RequeueTx(uint256 hash, std::chrono::microseconds request_time);

    // We sent this peer a GETDATA for this transaction. Save the expiry
    // time so we can expire it if the peer doesn't respond.
    void SetRequestExpiry(uint256 hash, std::chrono::microseconds expiry_time);

    // Don't request too many transactions from a peer simultaneously.
    bool MaxRequestInFlight();

    // Transaction has either been received or expired. No longer request
    // it from this peer.
    void RemoveTx(uint256 hash);

    // For robustness, expire old requests after a long timeout, so that
    // we can resume downloading transactions from a peer even if they
    // were unresponsive in the past.
    void ExpireOldAnnouncedTxs(std::chrono::microseconds current_time, NodeId nodeid);

    // Get a list of all transactions that are ready to be requested.
    void GetAnnouncedTxsToRequest(std::chrono::microseconds current_time, std::vector<uint256>& txs_to_request);
};
#endif // BITCOIN_NET_PROCESSING_H
