// Copyright (c) 2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/consensus.h>
#include <miner.h>
#include <script/script.h>
#include <txrebroadcast.h>

/** We rebroadcast 3/4 of max block weight to reduce noise due to circumstances
 *  such as miners mining priority transactions. */
static constexpr unsigned int MAX_REBROADCAST_WEIGHT = 3 * MAX_BLOCK_WEIGHT / 4;

std::vector<uint256> TxRebroadcastCalculator::GetRebroadcastTransactions(bool is_wtxid)
{
    std::vector<uint256> rebroadcast_txs;

    BlockAssembler::Options options;
    options.nBlockMaxWeight = MAX_REBROADCAST_WEIGHT;
    CScript dummy_script = CScript();

    // Use CreateNewBlock to identify rebroadcast candidates
    std::unique_ptr<CBlockTemplate> block_template = BlockAssembler(m_mempool, Params(), options).CreateNewBlock(dummy_script);

    LOCK(m_mempool.cs);
    for (const CTransactionRef& tx : block_template->block.vtx) {
        uint256 txhsh = is_wtxid ? tx->GetWitnessHash() : tx->GetHash();

        // Confirm the transaction is still in the mempool
        CTxMemPool::indexed_transaction_set::const_iterator it = is_wtxid ? m_mempool.get_iter_from_wtxid(txhsh) : m_mempool.mapTx.find(txhsh);
        if (it == m_mempool.mapTx.end()) continue;

        rebroadcast_txs.push_back(txhsh);
    }

    return rebroadcast_txs;
}
