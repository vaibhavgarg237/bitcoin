// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXREBROADCAST_H
#define BITCOIN_TXREBROADCAST_H

#include <tuple>
#include <txmempool.h>

struct TxIds
{
    TxIds(uint256 txid, uint256 wtxid) : m_txid(txid), m_wtxid(wtxid) {}

    const uint256 m_txid;
    const uint256 m_wtxid;
};

class TxRebroadcastHandler
{
public:
    TxRebroadcastHandler(CTxMemPool& mempool) : m_mempool(mempool) {};

    std::vector<TxIds> GetRebroadcastTransactions();

private:
    const CTxMemPool& m_mempool;
};

#endif // BITCOIN_TXREBROADCAST_H
