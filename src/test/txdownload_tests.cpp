// Copyright (c) 2011-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <net_processing.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(txdownload_tests)

BOOST_AUTO_TEST_CASE(txdownload)
{
    TxDownloadState myDownloadState;

    uint256 tx1 = InsecureRand256();
    uint256 tx2 = InsecureRand256();
    uint256 tx3 = InsecureRand256();

    myDownloadState.AddAnnouncedTx(tx1, std::chrono::microseconds(1000) /*time to attempt request*/, std::chrono::microseconds(9999) /*deadline to force request*/);
    myDownloadState.AddAnnouncedTx(tx2, std::chrono::microseconds(1500) /*time to attempt request*/, std::chrono::microseconds(9999) /*deadline to force request*/);
    myDownloadState.AddAnnouncedTx(tx3, std::chrono::microseconds(2000) /*time to attempt request*/, std::chrono::microseconds(9999) /*deadline to force request*/);

    std::vector<std::pair<uint256, bool>> txs;
    myDownloadState.GetAnnouncedTxsToRequest(std::chrono::microseconds(1500), txs);

    BOOST_CHECK(2 == txs.size());

    /* std::chrono::microseconds a_t2(4000); */
    /* myDownloadState.RequeueTx(a, a_t2); */

    /* std::chrono::microseconds b_t2(5000); */
    /* myDownloadState.SetRequestExpiry(b, b_t2); */

    /* myDownloadState.RemoveTx(c); */

    /* uint256 d = 4; */
    /* std::chrono::microseconds d_t(4000); */
    /* myDownloadState.AddAnnouncedTx(d, d_t); */
    /* uint256 e = 5; */
    /* std::chrono::microseconds e_t(5000); */
    /* myDownloadState.AddAnnouncedTx(e, e_t); */

    /* myDownloadState.SetRequestExpiry(a, d_t); */
    /* myDownloadState.SetRequestExpiry(d, d_t); */
    /* myDownloadState.SetRequestExpiry(e, d_t); */

    /* std::chrono::microseconds exp(4500); */
    /* myDownloadState.ExpireOldAnnouncedTxs(exp); */
}

BOOST_AUTO_TEST_SUITE_END()
