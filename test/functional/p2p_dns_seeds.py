#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test ThreadDNSAddressSeed logic for querying DNS seeds."""

from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework


class P2PDNSSeeds(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-dnsseed=1"]]

    def run_test(self):
        self.existing_outbound_connections_test()
        self.existing_block_relay_connections_test()
        self.force_dns_test()
        self.init_arg_tests()

    def existing_outbound_connections_test(self):
        # Make sure addrman is populated to enter the conditional where we
        # delay and potentially skip DNS seeding.
        self.nodes[0].addpeeraddress("192.0.0.8", 8333)

        self.log.info("Check that we *do not* query DNS seeds if we have 2 outbound connections")

        self.restart_node(0)
        with(self.nodes[0].assert_debug_log(expected_msgs=["P2P peers available. Skipped DNS seeding."], timeout=12)):
            for i in range(2):
                self.nodes[0].add_outbound_p2p_connection(P2PInterface(), p2p_idx=i, connection_type="outbound-full-relay")

    def existing_block_relay_connections_test(self):
        # Make sure addrman is populated to enter the conditional where we
        # delay and potentially skip DNS seeding.
        self.nodes[0].addpeeraddress("192.0.0.8", 8333)

        self.log.info("Check that we *do* query DNS seeds if we only have 2 block-relay-only connections")

        self.restart_node(0)
        with(self.nodes[0].assert_debug_log(expected_msgs=["Loading addresses from DNS seed"], timeout=12)):
            # This mimics the "anchors" logic where nodes are likely to
            # reconnect to block-relay-only connections on startup.
            # Since we do not participate in addr relay with these connections,
            # we still want to query the DNS seeds.
            for i in range(2):
                self.nodes[0].add_outbound_p2p_connection(P2PInterface(), p2p_idx=i, connection_type="block-relay-only")

    def force_dns_test(self):
        self.log.info("Check that we query DNS seeds if -forcednsseed param is set")

        with(self.nodes[0].assert_debug_log(expected_msgs=["Loading addresses from DNS seed"], timeout=12)):
            # -dnsseed defaults to 1 in bitcoind, but 0 in the test framework,
            # so pass it explicitly here
            self.restart_node(0, ["-forcednsseed", "-dnsseed=1"])

        # Restore default for subsequent tests
        self.restart_node(0, ["-forcednsseed=0"])

    def init_arg_tests(self):
        self.log.info("Check that setting -connect disables -dnsseed by default")
        self.nodes[0].stop_node()
        with(self.nodes[0].assert_debug_log(expected_msgs=["DNS seeding disabled"])):
            self.start_node(0, ["-connect"])

        self.log.info("Check that running -connect and -dnsseed means DNS logic runs.")
        with(self.nodes[0].assert_debug_log(expected_msgs=["Loading addresses from DNS seed"], timeout=12)):
            self.restart_node(0, ["-connect=1", "-dnsseed=1"])

        self.log.info("Check that running -forcednsseed and -dnsseed=0 throws an error.")
        self.nodes[0].stop_node()
        self.nodes[0].assert_start_raises_init_error(
            expected_msg="Error: Cannot set -forcednsseed to true when setting -dnsseed to false.",
            extra_args=["-forcednsseed=1", "-dnsseed=0"],
        )

        self.log.info("Check that running -forcednsseed and -connect throws an error.")
        # -connect soft sets -dnsseed to false, so throws the same error
        self.nodes[0].stop_node()
        self.nodes[0].assert_start_raises_init_error(
            expected_msg="Error: Cannot set -forcednsseed to true when setting -dnsseed to false.",
            extra_args=["-forcednsseed=1", "-connect"],
        )

        # Restore default bitcoind settings
        self.restart_node(0, ["-forcednsseed=0", "-dnsseed=1", "-connect=0"])


if __name__ == '__main__':
    P2PDNSSeeds().main()
