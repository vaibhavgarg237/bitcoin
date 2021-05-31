#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test addr-fetch connections."""

from test_framework.messages import (
    CAddress,
    NODE_NETWORK,
    NODE_WITNESS,
    msg_addr,
)
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

import time


class P2PAddrFetch(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_addr_msg(self, num):
        addrs = []
        for i in range(num):
            addr = CAddress()
            addr.time = int(time.time()) + i
            addr.nServices = NODE_NETWORK | NODE_WITNESS
            addr.ip = "123.123.123.2"
            addr.port = 8333 + i
            addrs.append(addr)

        msg = msg_addr()
        msg.addrs = addrs
        return msg

    def run_test(self):
        assert_equal(len(self.nodes[0].getpeerinfo()), 0)
        conn = self.nodes[0].add_outbound_p2p_connection(P2PInterface(), p2p_idx=0, connection_type="addr-fetch")

        # sanity checks
        info = self.nodes[0].getpeerinfo()
        assert_equal(len(info), 1)
        assert_equal(info[0]['connection_type'], 'addr-fetch')

        # check that we don't disconnect if connection sends 1 addr
        msg = self.setup_addr_msg(1)
        conn.send_and_ping(msg)
        assert_equal(len(self.nodes[0].getpeerinfo()), 1)

        # check that we do disconnect if connection sends many addrs
        msg = self.setup_addr_msg(10)
        conn.send_message(msg)
        self.wait_until(lambda: len(self.nodes[0].getpeerinfo()) == 0)


if __name__ == '__main__':
    P2PAddrFetch().main()
