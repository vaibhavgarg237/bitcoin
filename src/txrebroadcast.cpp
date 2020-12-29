// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/consensus.h>
#include <miner.h>
#include <script/script.h>
#include <txrebroadcast.h>
#include <util/time.h>
#include <validation.h>

/** We rebroadcast 3/4 of max block weight to reduce noise due to circumstances
 *  such as miners mining priority transactions. */
static constexpr unsigned int MAX_REBROADCAST_WEIGHT{3 * MAX_BLOCK_WEIGHT / 4};

/** Default minimum age for a transaction to be rebroadcast */
static constexpr std::chrono::minutes REBROADCAST_MIN_TX_AGE{30min};

std::vector<TxIds> TxRebroadcastHandler::GetRebroadcastTransactions()
{
    std::vector<TxIds> rebroadcast_txs;

    // If the cache has run since we received the last block, the fee rate
    // condition will not filter out any transactions, so skip this run.
    if (m_tip_at_cache_time == m_chainman.ActiveTip()) return rebroadcast_txs;

    BlockAssembler::Options options;
    options.nBlockMaxWeight = MAX_REBROADCAST_WEIGHT;
    options.m_skip_inclusion_until = GetTime<std::chrono::microseconds>() - REBROADCAST_MIN_TX_AGE;
    options.m_check_block_validity = false;
    options.blockMinFeeRate = m_cached_fee_rate;

    // Use CreateNewBlock to identify rebroadcast candidates
    auto block_template = BlockAssembler(m_chainman.ActiveChainstate(), m_mempool, m_chainparams, options)
                              .CreateNewBlock(CScript());

    for (const CTransactionRef& tx : block_template->block.vtx) {
        if (tx->IsCoinBase()) continue;

        rebroadcast_txs.push_back(TxIds(tx->GetHash(), tx->GetWitnessHash()));
    }

    return rebroadcast_txs;
};

void TxRebroadcastHandler::CacheMinRebroadcastFee()
{
    if (m_chainman.ActiveChainstate().IsInitialBlockDownload()) return;

    // Update stamp of chain tip on cache run
    m_tip_at_cache_time = m_chainman.ActiveTip();

    // Update cache fee rate
    m_cached_fee_rate = BlockAssembler(m_chainman.ActiveChainstate(), m_mempool, m_chainparams).MinTxFeeRate();
};
