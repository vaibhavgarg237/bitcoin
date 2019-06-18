#!/usr/bin/env python3
from io import BytesIO

from test_framework.messages import CTransaction
from test_framework.script import CScript
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, hex_str_to_bytes
from test_framework.blocktools import create_transaction
from feature_nulldummy import trueDummy
import pdb

class MalleabilityTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [['-acceptnonstdtxn=0']]

    def check_mempool_result(self, result_expected, rawtxs):
        result_test = self.nodes[0].testmempoolaccept(rawtxs, True)
        assert_equal(result_expected, result_test)

    def run_test(self):
        node = self.nodes[0]

        self.address = node.getnewaddress(address_type='legacy')
        self.ms_address = node.addmultisigaddress(1, [self.address], '', 'legacy')['address']

        self.log.info('Start with 200 blocks, and some unspent coins')
        assert_equal(node.getblockcount(), 200)
        coins = node.listunspent() # It's free real estate.

        self.log.info('Sending 0.3 BTC to an address...')

        # Create and sign a raw transaction
        tx = create_transaction(node, coins.pop()['txid'], self.ms_address, amount=0.3)
        raw_tx_0 = tx.serialize().hex()
        txid_0 = tx.rehash()

        # Check that this transaction is allowed in the mempool
        self.check_mempool_result(
            [{
                'txid': txid_0,
                'allowed': True
            }],
            [raw_tx_0],
        )

        # Actually add the transaction to the mempool
        node.sendrawtransaction(raw_tx_0, True)

        # Check that the transaction can't be added again because it is already
        # in the mempool
        self.check_mempool_result(
            [{
                'txid': txid_0,
                'allowed': False,
                'reject-reason': '18: txn-already-in-mempool'
            }],
            [raw_tx_0],
        )

        # 1. Create a standard transaction that is to be malleated.
        tx1 = create_transaction(node, txid_0, self.ms_address, amount=0.2)
        raw_tx_1 = tx1.serialize().hex()
        txid_1 = tx1.rehash()

        # 2. Check that the transaction is cool.
        self.check_mempool_result(
            [{
                'txid': txid_1,
                'allowed': True
            }],
            [raw_tx_1],
        )

        # 3. Malleate it.
        trueDummy(tx1)

        raw_tx_1 = tx1.serialize().hex()
        txid_1 = tx1.rehash()

        # 4. Check that it's not cool anymore. Should show up as rejection
        # reason 64. Where in the Bitcoin Core codebase are these rejections
        # coming from?
        self.check_mempool_result(
            [{
                'txid': txid_1,
                'allowed': False,
                'reject-reason': '64: non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)'
            }],
            [raw_tx_1],
        )

        # 5. Make the node accept non standard transactions
        extra_args = ['-acceptnonstdtxn=1']
        self.restart_node(0, extra_args)

        # 6. Check that the malleation will now make it into the mempool
        self.check_mempool_result(
            [{
                'txid': txid_1,
                'allowed': True
            }],
            [raw_tx_1],
        )

if __name__ == '__main__':
    MalleabilityTest().main()