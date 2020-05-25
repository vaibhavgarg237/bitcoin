#!/usr/bin/env python3
# Copyright (c) 2017-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test stuff."""

import time
from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than_or_equal,
    assert_greater_than,
    assert_raises_rpc_error,
    connect_nodes,
    p2p_port,
    wait_until,
)
from test_framework.mininode import P2PInterface
import test_framework.messages
from test_framework.messages import (
    CAddress,
    msg_addr,
    NODE_NETWORK,
    NODE_WITNESS,
)

class AddConnTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3

    def run_test(self):
        self.test_addconnection()

    def test_addconnection(self):
        # add node 2 to node 0
        ip_port = "127.0.0.1:{}".format(p2p_port(2))

        node0 = self.nodes[0]
        peerinfo = node0.getpeerinfo()

        self.log.info('ABCD start of test: {} peers'.format(len(peerinfo)))
        self.log.info('ABCD ip_port is {}'.format(ip_port))

        self.nodes[0].addconnection(ip_port, "auto")
        newpeerinfo = node0.getpeerinfo()
        self.log.info('ABCD end of test: {} peers'.format(len(newpeerinfo)))

        assert_equal(len(peerinfo) + 1, len(newpeerinfo))

        # to test:
        # if there's too many conns, does it throw an error?
        # make a block only connection
        # all the errors with wrong params

if __name__ == '__main__':
    AddConnTest().main()
