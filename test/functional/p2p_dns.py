#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test logic for querying DNS seeds."""

from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework


class P2PDNS(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        # self.extra_args = [["-dnsseed=1"]]

    def run_test(self):
        # add 2 block relay connections
        # self.nodes[0].add_outbound_p2p_connection(P2PInterface(), p2p_idx=0, connection_type="block-relay-only")
        # self.nodes[0].add_outbound_p2p_connection(P2PInterface(), p2p_idx=1, connection_type="block-relay-only")

        self.nodes[0].addpeeraddress("192.0.0.8", 8333)  # not reachable

        self.log.info("ABCD restart node")
        self.restart_node(0, extra_args=['-dnsseed=1'])

        # observe that we don't add any more connections
        # fails current master
        # ( also fails with the change )
        # with(self.nodes[0].assert_debug_log(expected_msgs=["ABCD P2P peers available. Skipped DNS seeding."], timeout=12)):
            # for i in range(2):
                # # passes everywhere
                # self.nodes[0].add_outbound_p2p_connection(P2PInterface(), p2p_idx=i, connection_type="outbound-full-relay")

                # fails in new, passes in old
                # self.nodes[0].add_outbound_p2p_connection(P2PInterface(), p2p_idx=i, connection_type="block-relay-only")

        # self.restart_node(0, extra_args=['dnsseed=1'])
        with(self.nodes[0].assert_debug_log(expected_msgs=["Loading addresses from DNS seed"], timeout=12)):
            for i in range(2):
                # passes in new, fails in old
                self.nodes[0].add_outbound_p2p_connection(P2PInterface(), p2p_idx=i, connection_type="block-relay-only")


if __name__ == '__main__':
    P2PDNS().main()
