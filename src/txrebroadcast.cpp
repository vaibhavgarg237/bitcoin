// Copyright (c) 2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <logging.h>
#include <miner.h>
#include <policy/feerate.h>
#include <script/script.h>
#include <txrebroadcast.h>
#include <validation.h>

/** We rebroadcast 3/4 of max block weight to reduce noise due to circumstances
 *  such as miners mining priority transactions. */
static constexpr unsigned int MAX_REBROADCAST_WEIGHT = 3 * MAX_BLOCK_WEIGHT / 4;

/* Default minimum age for a transaction to be rebroadcast */
static constexpr std::chrono::minutes REBROADCAST_MIN_TX_AGE = std::chrono::minutes{30};

std::vector<uint256> TxRebroadcastCalculator::GetRebroadcastTransactions(bool is_wtxid)
{
    std::vector<uint256> rebroadcast_txs;

    BlockAssembler::Options options;
    options.nBlockMaxWeight = MAX_REBROADCAST_WEIGHT;
    options.m_skip_inclusion_until = GetTime<std::chrono::seconds>() - REBROADCAST_MIN_TX_AGE;
    CScript dummy_script = CScript();

    // debugging: print out nTime of all mempool transactions
    {
        LOCK(m_mempool.cs);
        LogPrintf("mempool size: %d\n", m_mempool.size());
        int i = 1;
        for (CTxMemPool::indexed_transaction_set::const_iterator it = m_mempool.mapTx.begin(); it != m_mempool.mapTx.end(); it++) {
            LogPrintf("time of mempool txn [%d]: %d\n", i, it->GetTime().count());
            i++;
        }
    }

    // Use CreateNewBlock to identify rebroadcast candidates
    std::unique_ptr<CBlockTemplate> block_template = BlockAssembler(m_mempool, Params(), options).CreateNewBlock(dummy_script, /* check_block_validity */ false);

    LOCK(m_mempool.cs);
    int count = 0;
    for (const CTransactionRef& tx : block_template->block.vtx) {
        uint256 txhsh = is_wtxid ? tx->GetWitnessHash() : tx->GetHash();

        // Confirm the transaction is still in the mempool
        CTxMemPool::indexed_transaction_set::const_iterator it = is_wtxid ? m_mempool.get_iter_from_wtxid(txhsh) : m_mempool.mapTx.find(txhsh);
        if (it == m_mempool.mapTx.end()) continue;

        // Compare transaction fee rate to cached value
        CFeeRate fee_rate = CFeeRate(it->GetModifiedFee(), GetTransactionWeight(*tx));
        if (fee_rate > m_cached_fee_rate) {
            rebroadcast_txs.push_back(txhsh);
            count += 1;
        }
    }

    LogPrint(BCLog::NET, "%d rebroadcast candidates identified, from %s candidates filtered with cached fee rate of %s.\n", count, block_template->block.vtx.size(), m_cached_fee_rate.ToString());
    return rebroadcast_txs;
}

void TxRebroadcastCalculator::CacheMinRebroadcastFee()
{
    // Update time of next run
    m_next_min_fee_cache = GetTime<std::chrono::seconds>() + REBROADCAST_FEE_RATE_CACHE_INTERVAL;

    // Update stamp of chain tip on cache run
    m_tip_at_cache_time = ::ChainActive().Tip();

    // Update cache fee rate
    m_cached_fee_rate = BlockAssembler(m_mempool, Params()).minTxFeeRate();

    LogPrint(BCLog::NET, "Rebroadcast cached_fee_rate has been updated to=%s\n", m_cached_fee_rate.ToString());
}
