#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test p2p blocksonly"""
import time
from test_framework.blocktools import create_transaction, create_raw_transaction
from test_framework.messages import msg_tx, CTransaction, FromHex
from test_framework.mininode import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
import pdb

class P2PBlocksOnly(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = False
        self.num_nodes = 1
        self.extra_args = [["-blocksonly"]]
        self.round = 0

    def run_test(self):
        # self.blocks_only_tests()
        # self.blocks_only_tests(conn_type="outbound")
        # self.scenario1()
        # self.scenario2()
        self.scenario3()

    def scenario1(self):
        self.log.info("node in blocksonly mode, make a conn, send them a txn")
        node = self.nodes[0]
        node.add_p2p_connection(P2PInterface())

        # create a signed transaction
        input_txid = node.getblock(node.getblockhash(1) ,2)['tx'][0]['txid']
        tx = create_transaction(node, input_txid, node.getnewaddress(), amount=50-0.001)

        # they should disconnect
        node.p2p.send_message(msg_tx(tx))
        node.p2p.wait_for_disconnect()

    def scenario2(self):
        self.log.info("node in blocksonly mode, make a whitelisted conn, send them a txn")
        self.restart_node(0, ["-whitelist=127.0.0.1", "-blocksonly"])
        node = self.nodes[0]
        node.add_p2p_connection(P2PInterface())

        # create a signed transaction
        input_txid = node.getblock(node.getblockhash(1) ,2)['tx'][0]['txid']
        tx = create_transaction(node, input_txid, node.getnewaddress(), amount=50-0.001)

        # they should disconnect
        node.p2p.send_message(msg_tx(tx))
        node.p2p.wait_for_disconnect()

    def scenario3(self):
        self.log.info("node in blocksonly mode, -whitelistrelay enabled, make a whitelisted conn, send them a txn")
        self.restart_node(0, ["-whitelist=127.0.0.1", "-whitelistrelay", "-blocksonly"])
        node = self.nodes[0]
        node.add_p2p_connection(P2PInterface())

        # create a signed transaction
        input_txid = node.getblock(node.getblockhash(1) ,2)['tx'][0]['txid']
        tx = create_transaction(node, input_txid, node.getnewaddress(), amount=50-0.001)
        txid = tx.rehash()

        # they don't disconnect
        node.p2p.send_message(msg_tx(tx))
        time.sleep(5)
        assert_equal(node.p2p.is_connected, True)

        # but they also don't get the message
        node.p2p.wait_for_tx(txid)

        # self.restart_node(0, ["-persistmempool=0", "-whitelist=127.0.0.1", "-whitelistforcerelay", "-blocksonly"])
    def blocks_only_tests(self, conn_type=None):
        node = self.nodes[0]

        # check node is running in -blocksonly mode
        assert_equal(node.getnetworkinfo()['localrelay'], False)

        self.log.info('-blocksonly node with inbound connection')
        node.add_p2p_connection(P2PInterface(), connection_type=conn_type)

        # create a signed transaction
        input_txid = node.getblock(node.getblockhash(1) ,2)['tx'][0]['txid']
        tx = create_transaction(node, input_txid, node.getnewaddress(), amount=50-0.001)
        txid = tx.rehash()
        tx_hex = tx.serialize().hex()

        import pdb; pdb.set_trace()
        # on this connection, fRelayTxes is true

        # what happens if you sendrawtransaction?

        # understand difference between fRelayTxes and how is block relay connection negotitated?
        # what does a node that starts up in blocksonly mode do?

        # for block-relay-only connections: there's an optional `relay` boolean at the end of the VERSION message
        self.log.info('Send transaction & verify connection disconnects')
        with node.assert_debug_log(['transaction sent in violation of protocol peer=0']):
            node.p2p.send_message(msg_tx(tx))
            node.p2p.wait_for_disconnect()
            assert_equal(node.getmempoolinfo()['size'], 0)

        # Remove the disconnected peer and add a new one
        node.disconnect_p2ps()
        node.add_p2p_connection(P2PInterface(), connection_type=conn_type)

        self.log.info('Check that txs from rpc are accepted and relayed to other peers')
        # how is relaytxes true if node is in blocksonly mode??
        assert_equal(node.getpeerinfo()[0]['relaytxes'], True)
        with node.assert_debug_log(['received getdata for: tx {} peer=1'.format(txid)]):
            node.sendrawtransaction(tx_hex)
            node.p2p.wait_for_tx(txid)
            assert_equal(self.nodes[0].getmempoolinfo()['size'], 1)

        self.log.info('Test behavior with whitelisted peers')
        # wait, so you start in blocksonly mode & have a whitelisted peer, that peer sends you a transaction, you send it forward.
        # shouldn't there be a test that checks if you have a peer thats not whitelisted, you don't forward it on?
        # also, that means that in blocks-relay-only connections, if there's a transaction sent, it shouldn't disconnect?
        # unless there's a difference between the p2p connection for the two?

        # Restarting node 0 with whitelist permission and blocksonly
        self.restart_node(0, ["-persistmempool=0", "-whitelist=127.0.0.1", "-whitelistforcerelay", "-blocksonly"])
        assert_equal(node.getrawmempool(),[])

        whitelisted_peer = node.add_p2p_connection(P2PInterface(), connection_type=conn_type)
        other_peer = node.add_p2p_connection(P2PInterface(), connection_type=conn_type)

        whitelisted_peer_info = node.getpeerinfo()[0]
        assert_equal(whitelisted_peer_info['whitelisted'], True)
        assert_equal(whitelisted_peer_info['permissions'], ['noban', 'forcerelay', 'relay', 'mempool'])

        # other_peer_info = node.getpeerinfo()[1]
        # assert_equal(other_peer_info['whitelisted'], True)
        # assert_equal(other_peer_info['permissions'], ['noban', 'forcerelay', 'relay', 'mempool'])
        # it should send to a not white listed peer too, so maybe this could be a 2nd node

        mempool_result = node.testmempoolaccept([tx_hex])[0]
        assert_equal(mempool_result['allowed'], True)

        self.log.info('Check that the tx from whitelisted peer is relayed to others (ie.second_peer)')
        with node.assert_debug_log(["received getdata"]):
            whitelisted_peer.send_message(msg_tx(tx))
            self.log.info('Check that the whitelisted peer is still connected after sending the transaction')
            assert_equal(whitelisted_peer.is_connected, True)
            other_peer.wait_for_tx(mempool_result['txid'])
            assert_equal(node.getmempoolinfo()['size'], 1)

        self.log.info("Whitelisted peer's transaction is accepted and relayed")


if __name__ == '__main__':
    P2PBlocksOnly().main()
