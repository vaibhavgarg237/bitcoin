// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/consensus.h>
#include <logging.h>
#include <miner.h>
#include <script/script.h>
#include <txrebroadcast.h>
#include <util/time.h>
#include <validation.h>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

/** We rebroadcast 3/4 of max block weight to reduce noise due to circumstances
 *  such as miners mining priority transactions. */
static constexpr unsigned int MAX_REBROADCAST_WEIGHT{3 * MAX_BLOCK_WEIGHT / 4};

/** Default minimum age for a transaction to be rebroadcast */
static constexpr std::chrono::minutes REBROADCAST_MIN_TX_AGE{30min};

/** Maximum number of times we will rebroadcast a transaction */
static constexpr int MAX_REBROADCAST_COUNT{6};

/** Minimum amount of time between returning the same transaction for
 * rebroadcast */
static constexpr std::chrono::hours MIN_REATTEMPT_INTERVAL{4h};

/** The maximum number of entries permitted in m_attempt_tracker */
static constexpr int MAX_ENTRIES{500};

/** The maximum age of an entry ~3 months */
static constexpr std::chrono::hours MAX_ENTRY_AGE{24h * 30 * 3};

struct RebroadcastEntry {
    RebroadcastEntry(std::chrono::microseconds now_time, uint256 wtxid)
        : m_last_attempt(now_time),
          m_wtxid(wtxid) {}

    std::chrono::microseconds m_last_attempt;
    const uint256 m_wtxid;
    int m_count{1};
};

/** Used for multi_index tag  */
struct index_by_last_attempt {};

class indexed_rebroadcast_set : public
boost::multi_index_container<
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
>{};

TxRebroadcastHandler::~TxRebroadcastHandler() = default;

TxRebroadcastHandler::TxRebroadcastHandler(const CTxMemPool& mempool, const ChainstateManager& chainman, const CChainParams& chainparams)
    : m_mempool{mempool},
      m_chainman{chainman},
      m_chainparams(chainparams),
      m_attempt_tracker{std::make_unique<indexed_rebroadcast_set>()}{}

std::vector<TxIds> TxRebroadcastHandler::GetRebroadcastTransactions()
{
    std::vector<TxIds> rebroadcast_txs;
    auto start_time = GetTime<std::chrono::microseconds>();

    // If the cache has run since we received the last block, the fee rate
    // condition will not filter out any transactions, so skip this run.
    if (m_tip_at_cache_time == m_chainman.ActiveTip()) return rebroadcast_txs;

    BlockAssembler::Options options;
    options.nBlockMaxWeight = MAX_REBROADCAST_WEIGHT;
    options.m_skip_inclusion_until = start_time - REBROADCAST_MIN_TX_AGE;
    options.m_check_block_validity = false;
    options.blockMinFeeRate = m_cached_fee_rate;

    // Use CreateNewBlock to identify rebroadcast candidates
    auto block_template = BlockAssembler(m_chainman.ActiveChainstate(), m_mempool, m_chainparams, options)
                              .CreateNewBlock(CScript());
    auto after_cnb_time = GetTime<std::chrono::microseconds>();

    for (const CTransactionRef& tx : block_template->block.vtx) {
        if (tx->IsCoinBase()) continue;

        uint256 txid = tx->GetHash();
        uint256 wtxid = tx->GetWitnessHash();

        // Check if we have previously rebroadcasted, decide if we will this
        // round, and if so, record the attempt.
        auto entry_it = m_attempt_tracker->find(wtxid);

        if (entry_it == m_attempt_tracker->end()) {
            // No existing entry, we will rebroadcast, so create a new one
            RebroadcastEntry entry(start_time, wtxid);
            m_attempt_tracker->insert(entry);
        } else if (entry_it->m_count >= MAX_REBROADCAST_COUNT) {
            // We have already rebroadcast this transaction the maximum number
            // of times permitted, so skip rebroadcasting.
            continue;
        } else if (entry_it->m_last_attempt > start_time - MIN_REATTEMPT_INTERVAL) {
            // We already rebroadcasted this in the past 4 hours. Even if we
            // added it to the set, it would probably not get INVed to most
            // peers due to filterInventoryKnown.
            continue;
        } else {
            // We have rebroadcasted this transaction before, but will try
            // again now. Record the attempt.
            auto UpdateRebroadcastEntry = [start_time](RebroadcastEntry& rebroadcast_entry) {
                rebroadcast_entry.m_last_attempt = start_time;
                ++rebroadcast_entry.m_count;
            };

            m_attempt_tracker->modify(entry_it, UpdateRebroadcastEntry);
        }

        // Add to set of rebroadcast candidates
        rebroadcast_txs.push_back(TxIds(txid, wtxid));
    }

    TrimMaxRebroadcast();

    auto delta1 = after_cnb_time - start_time;
    auto delta2 = GetTime<std::chrono::microseconds>() - start_time;
    LogPrint(BCLog::BENCH, "GetRebroadcastTransactions(): %d us total, %d us spent in CreateNewBlock.\n", delta2.count(), delta1.count());
    LogPrint(BCLog::NET, "Queued %d transactions for attempted rebroadcast, filtered from %d candidates with cached fee rate of %s.\n", rebroadcast_txs.size(), block_template->block.vtx.size() - 1, m_cached_fee_rate.ToString(FeeEstimateMode::SAT_VB));

    for (TxIds ids : rebroadcast_txs) {
        LogPrint(BCLog::NET, "Attempting to rebroadcast txid: %s, wtxid: %s\n", ids.m_txid.ToString(), ids.m_wtxid.ToString());
    }

    return rebroadcast_txs;
};

void TxRebroadcastHandler::CacheMinRebroadcastFee()
{
    if (m_chainman.ActiveChainstate().IsInitialBlockDownload()) return;

    // Update stamp of chain tip on cache run
    m_tip_at_cache_time = m_chainman.ActiveTip();

    // Update cache fee rate
    auto start_time = GetTime<std::chrono::microseconds>();
    m_cached_fee_rate = BlockAssembler(m_chainman.ActiveChainstate(), m_mempool, m_chainparams).MinTxFeeRate();
    auto delta_time = GetTime<std::chrono::microseconds>() - start_time;
    LogPrint(BCLog::BENCH, "Caching minimum fee for rebroadcast to %s, took %d us to calculate.\n", m_cached_fee_rate.ToString(FeeEstimateMode::SAT_VB), delta_time.count());
};

void TxRebroadcastHandler::RemoveFromAttemptTracker(const CTransactionRef& tx) {
    const auto it = m_attempt_tracker->find(tx->GetWitnessHash());
    if (it == m_attempt_tracker->end()) return;
    m_attempt_tracker->erase(it);
}

void TxRebroadcastHandler::TrimMaxRebroadcast()
{
    // Delete any entries that are older than MAX_ENTRY_AGE
    auto min_age = GetTime<std::chrono::microseconds>() - MAX_ENTRY_AGE;

    while (!m_attempt_tracker->empty()) {
        auto it = m_attempt_tracker->get<index_by_last_attempt>().begin();
        if (it->m_last_attempt < min_age) {
            m_attempt_tracker->get<index_by_last_attempt>().erase(it);
        } else {
            break;
        }
    }

    // If there are still too many entries, delete the oldest ones
    while (m_attempt_tracker->size() > MAX_ENTRIES) {
        auto it = m_attempt_tracker->get<index_by_last_attempt>().begin();
        m_attempt_tracker->get<index_by_last_attempt>().erase(it);
    }
};

void TxRebroadcastHandler::UpdateAttempt(const uint256& wtxid, const int count, const std::chrono::microseconds last_attempt_time)
{
    auto it = m_attempt_tracker->find(wtxid);
    auto UpdateRebroadcastEntry = [last_attempt_time, count](RebroadcastEntry& rebroadcast_entry) {
        rebroadcast_entry.m_last_attempt = last_attempt_time;
        rebroadcast_entry.m_count += count;
    };

    m_attempt_tracker->modify(it, UpdateRebroadcastEntry);
};

bool TxRebroadcastHandler::CheckRecordedAttempt(const uint256& wtxid, const int expected_count, const std::chrono::microseconds expected_timestamp) const
{
    const auto it = m_attempt_tracker->find(wtxid);
    if (it == m_attempt_tracker->end()) return false;
    if (it->m_count != expected_count) return false;

    // Check the recorded timestamp is within 2 seconds of the param passed in
    std::chrono::microseconds delta = expected_timestamp - it->m_last_attempt;
    if (delta.count() > 2) return false;

    return true;
};

void TxRebroadcastHandler::UpdateCachedFeeRate(const CFeeRate& new_fee_rate)
{
    m_cached_fee_rate = new_fee_rate;
};

