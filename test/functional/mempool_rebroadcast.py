#!/usr/bin/env python3
# Copyright (c) 2009-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mempool rebroadcast logic.

"""

from test_framework.p2p import P2PTxInvStore
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    disconnect_nodes,
    connect_nodes,
    gen_return_txouts,
    create_confirmed_utxos,
    create_lots_of_big_transactions,
)
import time

# Constant from txmempool.h
MAX_REBROADCAST_WEIGHT = 3000000


class MempoolRebroadcastTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [[
            "-acceptnonstdtxn=1",
            "-blockmaxweight=3000000",
            "-whitelist=127.0.0.1",
            "-rebroadcast=1"
        ]] * self.num_nodes

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.test_simple_rebroadcast()
        self.test_rebroadcast_top_txns()
        self.test_recency_filter()

    def compare_txns_to_invs(self, txn_hshs, invs):
        tx_ids = [int(txhsh, 16) for txhsh in txn_hshs]

        assert_equal(len(tx_ids), len(invs))
        assert_equal(tx_ids.sort(), invs.sort())

    def test_simple_rebroadcast(self):
        self.log.info("Test simplest rebroadcast case")

        node0 = self.nodes[0]
        node1 = self.nodes[1]

        # Generate mempool transactions that both nodes know about
        for _ in range(3):
            node0.sendtoaddress(node1.getnewaddress(), 4)

        self.sync_all()

        # Generate mempool transactions that only node0 knows about
        disconnect_nodes(node0, 1)

        for _ in range(3):
            node0.sendtoaddress(node1.getnewaddress(), 5)

        # Check that mempools are different
        assert_equal(len(node0.getrawmempool()), 6)
        assert_equal(len(node1.getrawmempool()), 3)

        # Reconnect the nodes
        connect_nodes(node0, 1)

        # Rebroadcast will only occur if there has been a block since the last
        # run of CacheMinRebroadcastFee. When we connect a new peer,
        # rebroadcast will be skipped on the first run, but caching will
        # trigger. Have node1 generate a block so there are still mempool
        # transactions that need to be synched.
        node1.generate(1)

        assert_equal(len(node1.getrawmempool()), 0)
        self.wait_until(lambda: len(node0.getrawmempool()) == 3)

        # Bump time to hit rebroadcast interval
        mocktime = int(time.time()) + 300 * 60
        node0.setmocktime(mocktime)
        node1.setmocktime(mocktime)

        # Check that node1 got the transactions due to rebroadcasting
        self.wait_until(lambda: len(node1.getrawmempool()) == 3, timeout=30)

    def test_rebroadcast_top_txns(self):
        self.log.info("Testing that only transactions with top fee rates get rebroadcast")

        node = self.nodes[0]
        node.setmocktime(0)

        # Mine a block to clear out the mempool
        node.generate(1)
        assert_equal(len(node.getrawmempool()), 0)

        conn1 = node.add_p2p_connection(P2PTxInvStore())

        # Create transactions
        min_relay_fee = node.getnetworkinfo()["relayfee"]
        txouts = gen_return_txouts()
        utxo_count = 90
        utxos = create_confirmed_utxos(min_relay_fee, node, utxo_count)
        base_fee = min_relay_fee * 100  # Our transactions are smaller than 100kb
        txids = []

        # Create 3 batches of transactions at 3 different fee rate levels
        range_size = utxo_count // 3

        for i in range(3):
            start_range = i * range_size
            end_range = start_range + range_size
            txids.append(create_lots_of_big_transactions(node, txouts, utxos[start_range:end_range], end_range - start_range, (i + 1) * base_fee))

        # 90 transactions should be created
        # Confirm the invs were sent (initial broadcast)
        assert_equal(len(node.getrawmempool()), 90)
        self.wait_until(lambda: len(conn1.tx_invs_received) == 90)

        # Confirm transactions are more than max rebroadcast amount
        assert_greater_than(node.getmempoolinfo()['bytes'], MAX_REBROADCAST_WEIGHT)

        # Age transactions to ensure they won't be excluded due to recency filter
        mocktime = int(time.time()) + 31 * 60
        node.setmocktime(mocktime)

        # Add another p2p connection since txns aren't rebroadcast to the same peer (see filterInventoryKnown)
        conn2 = node.add_p2p_connection(P2PTxInvStore())

        # Trigger rebroadcast to occur
        mocktime += 300 * 60  # seconds
        node.setmocktime(mocktime)
        time.sleep(1)  # Ensure send message thread runs so invs get sent

        # `nNextInvSend` delay on `setInventoryTxToSend
        self.wait_until(lambda: conn2.get_invs(), timeout=30)

        global global_mocktime
        global_mocktime = mocktime

    def test_recency_filter(self):
        self.log.info("Test that recent transactions don't get rebroadcast")

        node = self.nodes[0]
        node1 = self.nodes[1]

        node.setmocktime(0)

        # Mine blocks to clear out the mempool
        node.generate(10)
        assert_equal(len(node.getrawmempool()), 0)

        # Add p2p connection
        conn = node.add_p2p_connection(P2PTxInvStore())

        # Create old transaction
        node.sendtoaddress(node.getnewaddress(), 2)
        assert_equal(len(node.getrawmempool()), 1)
        self.wait_until(lambda: conn.get_invs(), timeout=30)

        # Bump mocktime to ensure the transaction is old
        mocktime = int(time.time()) + 31 * 60  # seconds
        node.setmocktime(mocktime)

        delta_time = 28 * 60  # seconds
        while True:
            # Create a recent transaction
            new_tx = node1.sendtoaddress(node1.getnewaddress(), 2)
            new_tx_id = int(new_tx, 16)

            # Ensure node0 has the transaction
            self.wait_until(lambda: new_tx in node.getrawmempool())

            # Add another p2p connection since transactions aren't rebroadcast
            # to the same peer (see filterInventoryKnown)
            new_conn = node.add_p2p_connection(P2PTxInvStore())

            # Bump mocktime to try to get rebroadcast, but not so much that the
            # transaction would be old
            mocktime += delta_time
            node.setmocktime(mocktime)

            time.sleep(1.1)

            # Once we get any rebroadcasts, ensure the most recent transaction
            # is not included
            if new_conn.get_invs():
                assert(new_tx_id not in new_conn.get_invs())
                break


if __name__ == '__main__':
    MempoolRebroadcastTest().main()
