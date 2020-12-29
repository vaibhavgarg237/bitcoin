#!/usr/bin/env python3
# Copyright (c) 2009-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mempool rebroadcast logic.
"""

from test_framework.p2p import P2PTxInvStore
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


class MempoolRebroadcastTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [[
            "-whitelist=127.0.0.1",
            "-txindex"
        ]] * self.num_nodes

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

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

        # Form transaction & submit to mempool of both nodes directly
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

    def run_test(self):
        self.log.info("Test that the fee-rate cache limits the rebroadcast set")

        node = self.nodes[0]
        node1 = self.nodes[1]

        # Ensure we are starting from a clean slate
        assert_equal(len(node.p2ps), 0)

        mocktime = int(time.time())
        node.setmocktime(mocktime)
        node1.setmocktime(mocktime)

        # Create UTXOs that we can spend
        min_relay_fee = node.getnetworkinfo()["relayfee"]
        utxos = create_confirmed_utxos(min_relay_fee, node, 2000)

        addresses = []
        for _ in range(50):
            addresses.append(node.getnewaddress())

        # Create large transactions by sending to all the addresses
        outputs = {addr: 0.0001 for addr in addresses}
        change_address = node.getnewaddress()
        outputs_sum = 0.0001 * 50

        self.sync_mempools()

        cache_fee_rate = min_relay_fee * 3

        # Create lots of transactions with that large output
        for _ in range(len(utxos) - 1000):
            self.make_txn_at_fee_rate(utxos.pop(), outputs, outputs_sum, cache_fee_rate, change_address)

        self.sync_mempools()

        # Confirm we've created enough transactions to fill a cache block,
        # ensuring the threshold fee rate will be cache_fee_rate
        # Divide by 4 to convert from weight to virtual bytes
        assert_greater_than(node.getmempoolinfo()['bytes'], MAX_BLOCK_WEIGHT / 4)

        # Trigger a cache job run, see REBROADCAST_FEE_RATE_CACHE_INTERVAL
        mocktime += 21 * 60
        node.setmocktime(mocktime)
        node1.setmocktime(mocktime)
        time.sleep(1)

        # The cache job should have run by now. We won't rebroadcast unless
        # there has been a block since the last cache run.
        node.generate(1)

        self.log.info("Make high fee-rate transactions")
        high_fee_rate_tx_hshs = []
        high_fee_rate = min_relay_fee * 4

        for _ in range(10):
            tx_hsh = self.make_txn_at_fee_rate(utxos.pop(), outputs, outputs_sum, high_fee_rate, change_address)
            high_fee_rate_tx_hshs.append(tx_hsh)

        self.log.info("Make low fee-rate transactions")
        low_fee_rate_tx_hshs = []
        low_fee_rate = min_relay_fee * 2

        for _ in range(10):
            tx_hsh = self.make_txn_at_fee_rate(utxos.pop(), outputs, outputs_sum, low_fee_rate, change_address)
            low_fee_rate_tx_hshs.append(tx_hsh)

        # Ensure these transactions are removed from the unbroadcast set. Or in
        # other words, that all GETDATAs have been received before its time to
        # rebroadcast
        self.sync_mempools()

        # Confirm that the remaining bytes in the mempool are less than what
        # fits in the rebroadcast block. Otherwise, we could get a false
        # positive where the low_fee_rate transactions are not rebroadcast
        # simply because they do not fit, not because they were filtered out.
        assert_greater_than(3 * MAX_BLOCK_WEIGHT / 4, node.getmempoolinfo()['bytes'])

        self.log.info("Trigger rebroadcast")
        conn = node.add_p2p_connection(P2PTxInvStore())

        # To trigger the next rebroadcast run, we bump time more than
        # REBROADCAST_FEE_RATE_CACHE_INTERVAL. This means the next SendMessages run
        # for *any* peer will update the cached values. With two peers, the order
        # of events would be as such:
        # - peer0 rebroadcast
        # - update global cache values
        # - peer1 rebroadcast
        # This means for peer1, there will not be a block between the cache run
        # and the rebroadcast timer check.  Since we check the P2PConnection
        # for the rebroadcasted ids, we disconnect node1 to let the rebroadcast
        # logic trigger.
        self.disconnect_nodes(0, 1)

        # Bump time forward so m_next_rebroadcast_time hits
        mocktime += 300 * 60
        node.setmocktime(mocktime)

        # Give time for SendMessages thread to run so INVs get sent
        time.sleep(0.5)

        # Bump time forward so nNextInvSend timer pops
        mocktime += 5
        self.nodes[0].setmocktime(mocktime)

        self.wait_until(lambda: conn.get_invs(), timeout=30)
        rebroadcasted_invs = conn.get_invs()

        self.log.info("Check that high fee rate transactions are rebroadcast")
        for txhsh in high_fee_rate_tx_hshs:
            wtxhsh = node.getmempoolentry(txhsh)['wtxid']
            wtxid = int(wtxhsh, 16)
            assert(wtxid in rebroadcasted_invs)

        self.log.info("Check that low fee rate transactions are NOT rebroadcast")
        for txhsh in low_fee_rate_tx_hshs:
            wtxhsh = node.getmempoolentry(txhsh)['wtxid']
            wtxid = int(wtxhsh, 16)
            assert(wtxid not in rebroadcasted_invs)


if __name__ == '__main__':
    MempoolRebroadcastTest().main()
