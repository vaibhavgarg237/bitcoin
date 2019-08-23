// Copyright (c) 2021 The Bitcoin Core developers
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

std::vector<TxIds> TxRebroadcastHandler::GetRebroadcastTransactions()
{
    std::vector<TxIds> rebroadcast_txs;

    BlockAssembler::Options options;
    options.nBlockMaxWeight = MAX_REBROADCAST_WEIGHT;

    // Use CreateNewBlock to identify rebroadcast candidates
    auto block_template = BlockAssembler(m_mempool, Params(), options)
                          .CreateNewBlock(m_chainman.ActiveChainstate(), CScript());

    LOCK(m_mempool.cs);
    for (const CTransactionRef& tx : block_template->block.vtx) {
        if (tx->IsCoinBase()) continue;

        rebroadcast_txs.push_back(TxIds(tx->GetHash(), tx->GetWitnessHash()));
    }

    return rebroadcast_txs;
};
