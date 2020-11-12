#!/usr/bin/env python3
# Copyright (c) 2009-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mempool rebroadcast logic.
"""

from test_framework.p2p import (
    P2PConnection,
    P2PTxInvStore,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_approx,
    assert_equal,
    assert_greater_than,
    create_confirmed_utxos,
)
import time
from decimal import Decimal

# Constant from consensus.h
MAX_BLOCK_WEIGHT = 4000000

global_mocktime = 0


class MempoolRebroadcastTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [[
            "-whitelist=127.0.0.1",
            "-rebroadcast=1",
            "-txindex=1"
        ]] * self.num_nodes

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        global global_mocktime
        global_mocktime = int(time.time())

        self.test_simple_rebroadcast()
        # self.test_recency_filter()
        # self.test_fee_rate_cache()

    def make_txn_at_fee_rate(self, input_utxo, outputs, outputs_sum, desired_fee_rate, change_address):
        node = self.nodes[0]
        node1 = self.nodes[1]

        inputs = [{'txid': input_utxo['txid'], 'vout': input_utxo['vout']}]

        # Calculate how much input values add up to
        input_tx_hsh = input_utxo['txid']
        raw_tx = node.decoderawtransaction(node.getrawtransaction(input_tx_hsh))
        inputs_list = raw_tx['vout']
        if 'coinbase' in raw_tx['vin'][0].keys():
            return
        index = raw_tx['vin'][0]['vout']
        inputs_sum = inputs_list[index]['value']

        # Divide by 1000 because vsize is in bytes & cache fee rate is BTC / kB
        tx_vsize_with_change = 1660
        desired_fee_btc = desired_fee_rate * tx_vsize_with_change / 1000
        current_fee_btc = inputs_sum - Decimal(str(outputs_sum))

        # Add another output with change
        outputs[change_address] = float(current_fee_btc - desired_fee_btc)
        outputs_sum += outputs[change_address]

        # Form transaction & submit to the mempools of both nodes directly
        raw_tx_hex = node.createrawtransaction(inputs, outputs)
        signed_tx = node.signrawtransactionwithwallet(raw_tx_hex)
        tx_hsh = node.sendrawtransaction(hexstring=signed_tx['hex'], maxfeerate=0)
        node1.sendrawtransaction(hexstring=signed_tx['hex'], maxfeerate=0)

        # Retrieve mempool transaction to calculate fee rate
        mempool_entry = node.getmempoolentry(tx_hsh)

        # Check absolute fee matches up to expectations
        fee_calculated = inputs_sum - Decimal(str(outputs_sum))
        fee_got = mempool_entry['fee']
        assert_approx(float(fee_calculated), float(fee_got))

        # mempool_entry['fee'] is in BTC, fee rate should be BTC / kb
        fee_rate = mempool_entry['fee'] * 1000 / mempool_entry['vsize']
        assert_approx(float(fee_rate), float(desired_fee_rate))

        return tx_hsh

    def test_simple_rebroadcast(self):
        self.log.info("Test simplest rebroadcast case")

        global global_mocktime
        node = self.nodes[0]
        node1 = self.nodes[1]

        self.log.info("Trigger rebroadcast cache to set minimum fee to cache_fee_rate")
        min_relay_fee = node.getnetworkinfo()["relayfee"]
        cache_fee_rate = min_relay_fee * 3

        utxos = create_confirmed_utxos(min_relay_fee, node, 3000)

        # Create large outputs
        addresses = []
        for _ in range(50):
            addresses.append(node.getnewaddress())

        outputs = {addr: 0.0001 for addr in addresses}
        change_address = node.getnewaddress()
        outputs_sum = 0.0001 * 50

        self.sync_mempools()

        # Create transactions
        for _ in range(len(utxos) - 500):
            self.make_txn_at_fee_rate(utxos.pop(), outputs, outputs_sum, cache_fee_rate, change_address)

        self.sync_mempools()
        assert_greater_than(node.getmempoolinfo()['bytes'], MAX_BLOCK_WEIGHT)
        assert_equal(len(node.getrawmempool()), len(node1.getrawmempool()))

        # Ensure cache job runs, see REBROADCAST_FEE_RATE_CACHE_INTERVAL
        global_mocktime += 21 * 60
        node.setmocktime(global_mocktime)
        node1.setmocktime(global_mocktime)

        time.sleep(1)

        # The cache job should have run by now, and we won't rebroadcast unless
        # there has been a block since the last cache run.
        node1.generate(1)

        self.log.info("Disconnect nodes and create transactions that only node 0 knows about")
        self.disconnect_nodes(0, 1)

        # Add p2p connection to send a GETDATA and remove the transactions from the unbroadcast set
        conn = node.add_p2p_connection(P2PTxInvStore())

        # TODO: this is timing out
        for _ in range(3):
            node.sendtoaddress(node1.getnewaddress(), 0.5)

        # Bump time forward to ensure nNextInvSend timer pops
        global_mocktime += 5
        self.nodes[0].setmocktime(global_mocktime)
        self.nodes[1].setmocktime(global_mocktime)
        self.wait_until(lambda: conn.get_invs(), timeout=30)

        # Check that mempools are different by 3
        assert_equal(3, len(node.getrawmempool()) - len(node1.getrawmempool()))

        self.log.info("Reconnect nodes and check that transactions got rebroadcast")
        self.connect_nodes(0, 1)

        # Bump time to hit rebroadcast interval
        global_mocktime += 300 * 60
        node.setmocktime(global_mocktime)
        node1.setmocktime(global_mocktime)

        # Check that node1 got the transactions due to rebroadcasting and now
        # has the same size mempool as node0
        self.wait_until(lambda: len(node.getrawmempool()) == len(node1.getrawmempool()), timeout=30)

    def test_recency_filter(self):
        self.log.info("Test that recent transactions don't get rebroadcast")

        node = self.nodes[0]
        node1 = self.nodes[1]

        global global_mocktime
        node.setmocktime(global_mocktime)
        node1.setmocktime(global_mocktime)

        # Mine blocks to clear out the mempool
        node.generate(4)
        assert_equal(len(node.getrawmempool()), 0)

        # Add p2p connection
        # conn = node.add_p2p_connection(P2PTxInvStore())

        # Create initial transaction
        node.sendtoaddress(node.getnewaddress(), 2)
        assert_equal(len(node.getrawmempool()), 1)
        # self.wait_until(lambda: conn.get_invs(), timeout=30)
        self.wait_until(lambda: len(node1.getrawmempool()) == 1, timeout=30)

        # Bump mocktime to age the transaction
        global_mocktime += 31 * 60  # seconds
        node.setmocktime(global_mocktime)
        node1.setmocktime(global_mocktime)

        # debugging
        assert_equal(len(node.getrawmempool()), 1)
        assert_equal(len(node1.getrawmempool()), 1)
        assert_equal(node.getrawmempool(), node1.getrawmempool())

        # assert_equal(len(node.getpeerinfo()), 2)
        assert_equal(len(node1.getpeerinfo()), 1)

        delta_time = 28 * 60  # seconds
        while True:
            assert_equal(node.getrawmempool(), node1.getrawmempool())
            print("{} transactions in the mempools".format(len(node.getrawmempool())))

            # Create a recent transaction
            txhsh = node1.sendtoaddress(node1.getnewaddress(), 2)
            wtxhsh = node1.getmempoolentry(txhsh)['wtxid']
            new_wtxid = int(wtxhsh, 16)

            # Ensure node has the transaction, bump for next inv send delay
            global_mocktime += 5
            self.nodes[0].setmocktime(global_mocktime)
            self.nodes[1].setmocktime(global_mocktime)
            self.wait_until(lambda: txhsh in node.getrawmempool())

            node.disconnect_p2ps()

            # Add another p2p connection since transactions aren't rebroadcast
            # to the same peer (see filterInventoryKnown)
            new_conn = node.add_p2p_connection(P2PTxInvStore())

            assert_equal(len(node.p2ps), 1)

            # Bump mocktime to try to get rebroadcast, but not so much that the
            # transaction would be old
            global_mocktime += delta_time
            node.setmocktime(global_mocktime)

            time.sleep(1.1)

            # Once we get any rebroadcasts, ensure the most recent transaction
            # is not included
            if new_conn.get_invs():
                assert(new_wtxid not in new_conn.get_invs())
                break

        node.disconnect_p2ps()

    def test_fee_rate_cache(self):
        self.log.info("Test that min-fee-rate cache limits rebroadcast set")

        node = self.nodes[0]
        node1 = self.nodes[1]

        assert_equal(len(node.p2ps), 0)

        global_mocktime = int(time.time())
        node.setmocktime(global_mocktime)
        node1.setmocktime(global_mocktime)

        min_relay_fee = node.getnetworkinfo()["relayfee"]
        utxos = create_confirmed_utxos(min_relay_fee, node, 3000)

        addresses = []
        for _ in range(50):
            addresses.append(node.getnewaddress())

        # Create large transactions by sending to all the addresses
        outputs = {addr: 0.0001 for addr in addresses}
        change_address = node.getnewaddress()
        outputs_sum = 0.0001 * 50

        self.sync_mempools()

        self.log.info("Trigger rebroadcast cache to cache_fee_rate")
        # initial_tx_hshs = []
        cache_fee_rate = min_relay_fee * 3

        # Create lots of transactions with that large output
        for _ in range(len(utxos) - 500):
            tx_hsh = self.make_txn_at_fee_rate(utxos.pop(), outputs, outputs_sum, cache_fee_rate, change_address)
            # initial_tx_hshs.append(tx_hsh)

        self.sync_mempools()
        assert_greater_than(node.getmempoolinfo()['bytes'], MAX_BLOCK_WEIGHT)

        # Ensure cache job runs, see REBROADCAST_FEE_RATE_CACHE_INTERVAL
        global_mocktime += 21 * 60
        node.setmocktime(global_mocktime)
        node1.setmocktime(global_mocktime)

        time.sleep(1)

        # The cache job should have run by now, and we won't rebroadcast unless
        # there has been a block since the last cache run.
        node.generate(1)

        self.log.info("Make high fee rate transactions")
        high_fee_rate_tx_hshs = []
        high_fee_rate = min_relay_fee * 4

        for _ in range(10):
            tx_hsh = self.make_txn_at_fee_rate(utxos.pop(), outputs, outputs_sum, high_fee_rate, change_address)
            high_fee_rate_tx_hshs.append(tx_hsh)

        self.log.info("Make low fee rate transactions")
        low_fee_rate_tx_hshs = []
        low_fee_rate = min_relay_fee * 2

        for _ in range(10):
            tx_hsh = self.make_txn_at_fee_rate(utxos.pop(), outputs, outputs_sum, low_fee_rate, change_address)
            low_fee_rate_tx_hshs.append(tx_hsh)

        # Ensure these transactions are removed from unbroadcast set. Or in
        # other words, that all the GETDATAs have been received before its time
        # to rebroadcast
        self.sync_mempools()

        self.log.info("Trigger rebroadcast")
        conn = node.add_p2p_connection(P2PTxInvStore())

        global_mocktime += 300 * 60
        node.setmocktime(global_mocktime)

        time.sleep(0.5)  # Ensure send message thread runs so invs get sent

        # Bump time forward to ensure nNextInvSend timer pops
        global_mocktime += 5
        self.nodes[0].setmocktime(global_mocktime)

        self.wait_until(lambda: conn.get_invs(), timeout=30)
        rebroadcasted_invs = conn.get_invs()

        # Check that top fee rate transactions are rebroadcast
        for txhsh in high_fee_rate_tx_hshs:
            wtxhsh = node.getmempoolentry(txhsh)['wtxid']
            wtxid = int(wtxhsh, 16)
            assert(wtxid in rebroadcasted_invs)

        # Check that low fee rate transactions are not rebroadcast
        for txhsh in low_fee_rate_tx_hshs:
            wtxhsh = node.getmempoolentry(txhsh)['wtxid']
            wtxid = int(wtxhsh, 16)
            assert(wtxid not in rebroadcasted_invs)


if __name__ == '__main__':
    MempoolRebroadcastTest().main()
