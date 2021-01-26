// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/consensus.h>
#include <miner.h>
#include <script/script.h>
#include <txrebroadcast.h>
#include <validation.h>

/** We rebroadcast 3/4 of max block weight to reduce noise due to circumstances
 *  such as miners mining priority transactions. */
static constexpr unsigned int MAX_REBROADCAST_WEIGHT = 3 * MAX_BLOCK_WEIGHT / 4;

/** Default minimum age for a transaction to be rebroadcast */
static constexpr std::chrono::minutes REBROADCAST_MIN_TX_AGE = 30min;

/** Maximum number of times we will rebroadcast a tranasaction */
static constexpr int MAX_REBROADCAST_COUNT = 6;

/** Minimum amount of time between returning the same transaction for
 * rebroadcast */
static constexpr std::chrono::hours MIN_REATTEMPT_INTERVAL = 4h;

/** The maximum number of entries permitted in m_attempt_tracker */
static constexpr int MAX_ENTRIES = 500;

/** The maximum age of an entry ~3 months */
static constexpr std::chrono::hours MAX_ENTRY_AGE = std::chrono::hours(3 * 30 * 24);

std::vector<TxIds> TxRebroadcastHandler::GetRebroadcastTransactions()
{
    std::vector<TxIds> rebroadcast_txs;
    std::chrono::microseconds start_time = GetTime<std::chrono::microseconds>();

    // If there has not been a cache run since the last block, the fee rate
    // condition will not filter out any transactions, so skip this run.
    if (m_tip_at_cache_time == ::ChainActive().Tip()) return rebroadcast_txs;

    BlockAssembler::Options options;
    options.nBlockMaxWeight = MAX_REBROADCAST_WEIGHT;
    options.m_skip_inclusion_until = start_time - REBROADCAST_MIN_TX_AGE;
    options.check_block_validity = false;
    options.blockMinFeeRate = m_cached_fee_rate;

    // Use CreateNewBlock to identify rebroadcast candidates
    auto block_template = BlockAssembler(m_mempool, Params(), options)
                          .CreateNewBlock(m_chainman.ActiveChainstate(), CScript());

    for (const CTransactionRef& tx : block_template->block.vtx) {
        if (tx->IsCoinBase()) continue;

        uint256 txid = tx->GetHash();
        uint256 wtxid = tx->GetWitnessHash();

        // Check if we have previously rebroadcasted, decide if we will this
        // round, and if so, record the attempt.
        auto entry_it = m_attempt_tracker.find(wtxid);

        if (entry_it == m_attempt_tracker.end()) {
            // No existing entry, we will rebroadcast, so create a new one
            RebroadcastEntry entry(start_time, wtxid);
            m_attempt_tracker.insert(entry);
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
            // again now.
            RecordAttempt(entry_it);
        }

        // Add to set of rebroadcast candidates
        rebroadcast_txs.push_back(TxIds(txid, wtxid));
    }

    TrimMaxRebroadcast();

    return rebroadcast_txs;
};

void TxRebroadcastHandler::CacheMinRebroadcastFee()
{
    // Update stamp of chain tip on cache run
    m_tip_at_cache_time = ::ChainActive().Tip();

    // Update cache fee rate
    m_cached_fee_rate = BlockAssembler(m_mempool, Params()).minTxFeeRate();
};

void TxRebroadcastHandler::RecordAttempt(indexed_rebroadcast_set::index<index_by_wtxid>::type::iterator& entry_it)
{
    auto UpdateRebroadcastEntry = [](RebroadcastEntry& rebroadcast_entry) {
        rebroadcast_entry.m_last_attempt = GetTime<std::chrono::microseconds>();
        ++rebroadcast_entry.m_count;
    };

    m_attempt_tracker.modify(entry_it, UpdateRebroadcastEntry);
};

void TxRebroadcastHandler::TrimMaxRebroadcast()
{
    // Delete any entries that are older than MAX_ENTRY_AGE
    std::chrono::microseconds min_age = GetTime<std::chrono::microseconds>() - MAX_ENTRY_AGE;

    while (!m_attempt_tracker.empty()) {
        auto it = m_attempt_tracker.get<index_by_last_attempt>().begin();
        if (it->m_last_attempt < min_age) {
            m_attempt_tracker.get<index_by_last_attempt>().erase(it);
        } else {
            break;
        }
    }

    // If there are still too many entries, delete the oldest ones
    while (m_attempt_tracker.size() > MAX_ENTRIES) {
        auto it = m_attempt_tracker.get<index_by_last_attempt>().begin();
        m_attempt_tracker.get<index_by_last_attempt>().erase(it);
    }
};
