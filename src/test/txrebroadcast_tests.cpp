// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <clientversion.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <key_io.h>
#include <rpc/util.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <tuple>
#include <txmempool.h>
#include <txrebroadcast.h>
#include <util/time.h>
#include <validation.h>

BOOST_FIXTURE_TEST_SUITE(txrebroadcast_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(recency)
{
    // Since the test chain comes with 100 blocks, the first coinbase is
    // already valid to spend. Generate another block to have two valid
    // coinbase inputs to spend.
    CreateAndProcessBlock(std::vector<CMutableTransaction>(), CScript());

    // Create a transaction
    CKey key;
    key.MakeNewKey(true);
    CScript output_destination = GetScriptForDestination(PKHash(key.GetPubKey()));
    CMutableTransaction tx_old = CreateValidMempoolTransaction(m_coinbase_txns[0], /* vout */ 0,  output_destination, CAmount(48 * COIN));

    // Age transaction by 35 minutes, to be older than REBROADCAST_MIN_TX_AGE
    SetMockTime(GetTime() + 35 * 60);

    // Create a recent transaction
    CMutableTransaction tx_new = CreateValidMempoolTransaction(m_coinbase_txns[1], /* vout */ 0, output_destination, CAmount(48 * COIN));

    // Confirm both transactions successfully made it into the mempool
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 2);

    // Run rebroadcast handler and confirm that only the old transaction is included
    TxRebroadcastHandler tx_rebroadcast(*m_node.mempool);
    std::vector<TxIds> candidates = tx_rebroadcast.GetRebroadcastTransactions();
    BOOST_CHECK_EQUAL(candidates.size(), 1);
    BOOST_CHECK_EQUAL(candidates.front().m_txid, tx_old.GetHash());
}

BOOST_AUTO_TEST_SUITE_END()
