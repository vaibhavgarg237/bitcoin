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

class TxRebroadcastHandlerTest : public TxRebroadcastHandler
{
public:
    TxRebroadcastHandlerTest(CTxMemPool& mempool, ChainstateManager& chainman) : TxRebroadcastHandler(mempool, chainman){};

    bool CheckRecordedAttempt(uint256 txhsh, int expected_count, std::chrono::microseconds expected_timestamp)
    {
        const auto it = m_attempt_tracker.find(txhsh);
        if (it == m_attempt_tracker.end()) return false;
        if (it->m_count != expected_count) return false;

        // Check the recorded timestamp is within 2 seconds of the param passed in
        std::chrono::microseconds delta = expected_timestamp - it->m_last_attempt;
        if (delta.count() > 2) return false;

        return true;
    };

    void UpdateAttempt(uint256 txhsh, int count)
    {
        auto it = m_attempt_tracker.find(txhsh);
        for (int i = 0; i < count; ++i) {
            RecordAttempt(it);
        }
    };

    void RecordAttempt(indexed_rebroadcast_set::index<index_by_wtxid>::type::iterator& entry_it)
    {
        auto UpdateRebroadcastEntry = [](RebroadcastEntry& rebroadcast_entry) {
            rebroadcast_entry.m_last_attempt = GetTime<std::chrono::microseconds>() - 4h;
            ++rebroadcast_entry.m_count;
        };

        m_attempt_tracker.modify(entry_it, UpdateRebroadcastEntry);
    };
};

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
    CMutableTransaction tx_old = CreateValidMempoolTransaction(m_coinbase_txns[0], /* vout */ 0, /* input_height */ 0, coinbaseKey, output_destination, CAmount(48 * COIN));

    // Age transaction to be older than REBROADCAST_MIN_TX_AGE
    SetMockTime(GetTime<std::chrono::seconds>() + 35min);

    // Create a recent transaction
    CMutableTransaction tx_new = CreateValidMempoolTransaction(m_coinbase_txns[1], /* vout */ 0, /* input_height */ 1, coinbaseKey, output_destination, CAmount(48 * COIN));

    // Confirm both transactions successfully made it into the mempool
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 2);

    // Instantiate rebroadcast module & mine a block, so when we run
    // GetRebroadcastTransactions, Chain tip will be beyond m_tip_at_cache_time
    TxRebroadcastHandler tx_rebroadcast(*m_node.mempool, *m_node.chainman);
    CreateAndProcessBlock(std::vector<CMutableTransaction>(), CScript());

    // Confirm that only the old transaction is included
    std::vector<TxIds> candidates = tx_rebroadcast.GetRebroadcastTransactions();
    BOOST_REQUIRE_EQUAL(candidates.size(), 1);
    BOOST_CHECK_EQUAL(candidates.front().m_txid, tx_old.GetHash());
}

BOOST_AUTO_TEST_CASE(max_rebroadcast)
{
    // Create a transaction
    CKey key;
    key.MakeNewKey(true);
    CScript output_destination = GetScriptForDestination(PKHash(key.GetPubKey()));
    CMutableTransaction tx = CreateValidMempoolTransaction(m_coinbase_txns[0], /* vout */ 0, /* input_height */ 0, coinbaseKey, output_destination, CAmount(48 * COIN));
    uint256 txhsh = tx.GetHash();

    // Instantiate rebroadcast module & mine a block, so when we run
    // GetRebroadcastTransactions, Chain tip will be beyond m_tip_at_cache_time
    TxRebroadcastHandlerTest tx_rebroadcast(*m_node.mempool, *m_node.chainman);
    CreateAndProcessBlock(std::vector<CMutableTransaction>(), CScript());

    // Age transaction by 35 minutes, to be older than REBROADCAST_MIN_TX_AGE
    std::chrono::seconds current_time = GetTime<std::chrono::seconds>();
    current_time += 35min;
    SetMockTime(current_time);

    // Check that the transaction gets returned to rebroadcast
    std::vector<TxIds> candidates = tx_rebroadcast.GetRebroadcastTransactions();
    BOOST_CHECK_EQUAL(candidates.size(), 1);
    BOOST_CHECK_EQUAL(candidates.front().m_txid, txhsh);

    // Check if transaction was properly added to m_attempt_tracker
    BOOST_CHECK(tx_rebroadcast.CheckRecordedAttempt(txhsh, 1, current_time));

    // Since the transaction was returned within the last
    // REBROADCAST_MIN_TX_AGE time, check it does not get returned again
    candidates = tx_rebroadcast.GetRebroadcastTransactions();
    BOOST_CHECK_EQUAL(candidates.size(), 0);
    // And that the m_attempt_tracker entry is not updated
    BOOST_CHECK(tx_rebroadcast.CheckRecordedAttempt(txhsh, 1, current_time));

    // Bump time by 4 hours, to pass the MIN_INTERVAL time
    current_time += 4h;
    SetMockTime(current_time);
    // Then check that it gets returned for rebroadacst
    candidates = tx_rebroadcast.GetRebroadcastTransactions();
    BOOST_CHECK_EQUAL(candidates.size(), 1);
    // And that m_attempt_tracker is properly updated
    BOOST_CHECK(tx_rebroadcast.CheckRecordedAttempt(txhsh, 2, current_time));

    // Update the record to have m_count to be MAX_REBROADCAST_COUNT, and last
    // attempt time of 4 hours ago
    tx_rebroadcast.UpdateAttempt(txhsh, 4);
    // Check that transaction is not rebroadcast
    candidates = tx_rebroadcast.GetRebroadcastTransactions();
    BOOST_CHECK_EQUAL(candidates.size(), 0);
    // And that the m_attempt_tracker entry is not updated
    BOOST_CHECK(tx_rebroadcast.CheckRecordedAttempt(txhsh, 6, current_time - 4h));
}

BOOST_AUTO_TEST_SUITE_END()
