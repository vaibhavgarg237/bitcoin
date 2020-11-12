#!/usr/bin/env python3
# Copyright (c) 2009-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mempool rebroadcast logic.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
)
import time


class MempoolRebroadcastTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [[
            "-whitelist=127.0.0.1",
        ]] * self.num_nodes

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Test simplest rebroadcast case")

        node0 = self.nodes[0]
        node1 = self.nodes[1]

        # Generate mempool transactions that both nodes know about
        for _ in range(3):
            node0.sendtoaddress(node1.getnewaddress(), 4)

        self.sync_all()

        # Generate mempool transactions that only node0 knows about
        self.disconnect_nodes(0, 1)

        for _ in range(3):
            node0.sendtoaddress(node1.getnewaddress(), 5)

        # Check that mempools are different
        assert_equal(len(node0.getrawmempool()), 6)
        assert_equal(len(node1.getrawmempool()), 3)

        # Reconnect the nodes
        self.connect_nodes(0, 1)

        # Bump time to hit rebroadcast timer (see TX_REBROADCAST_INTERVAL)
        # We don't rebroadcast on the first run and the timer is set based on a
        # Poisson distribution, so bump significantly to ensure we trigger
        mocktime = int(time.time()) + 300 * 60
        node0.setmocktime(mocktime)
        node1.setmocktime(mocktime)

        # Check that node1 got the transactions due to rebroadcasting
        self.wait_until(lambda: len(node1.getrawmempool()) == 6, timeout=30)


if __name__ == '__main__':
    MempoolRebroadcastTest().main()
