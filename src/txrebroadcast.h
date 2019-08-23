// Copyright (c) 2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXREBROADCAST_H
#define BITCOIN_TXREBROADCAST_H

#include <txmempool.h>
#include <uint256.h>
#include <util/time.h>

/** Average delay between rebroadcasts */
static constexpr auto TX_REBROADCAST_INTERVAL = 1h;

class TxRebroadcastCalculator
{
public:
    TxRebroadcastCalculator(CTxMemPool& mempool) : m_mempool(mempool) {};

    /** Identify rebroadcast candidates. We select the highest fee-rate
     * transactions in the mempool by using CreateNewBlock with specific
     * rebroadcast parameters.
     *
     *  @param wtxid Whether the set should return txids or wtxids.
     */
    std::vector<uint256> GetRebroadcastTransactions(bool is_wtxid);

private:
    CTxMemPool& m_mempool;
};

#endif // BITCOIN_TXREBROADCAST_H
