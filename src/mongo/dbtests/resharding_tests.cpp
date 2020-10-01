/**
 *    Copyright (C) 2020-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kTest

#include "mongo/platform/basic.h"

#include <memory>

#include "mongo/db/client.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/dbhelpers.h"
#include "mongo/db/global_settings.h"
#include "mongo/db/op_observer_impl.h"
#include "mongo/db/op_observer_registry.h"
#include "mongo/db/pipeline/expression_context_for_test.h"
#include "mongo/db/repl/oplog.h"
#include "mongo/db/repl/repl_client_info.h"
#include "mongo/db/repl/replication_consistency_markers_mock.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/repl/replication_coordinator_mock.h"
#include "mongo/db/repl/replication_process.h"
#include "mongo/db/repl/replication_recovery_mock.h"
#include "mongo/db/repl/storage_interface_impl.h"
#include "mongo/db/s/resharding/resharding_oplog_fetcher.h"
#include "mongo/db/s/resharding_util.h"
#include "mongo/db/service_context.h"
#include "mongo/db/storage/snapshot_manager.h"
#include "mongo/db/storage/storage_engine.h"
#include "mongo/db/storage/write_unit_of_work.h"
#include "mongo/db/vector_clock_mutable.h"
#include "mongo/dbtests/dbtests.h"
#include "mongo/logv2/log.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
namespace {
/**
 * RAII type for operating at a timestamp. Will remove any timestamping when the object destructs.
 */
class OneOffRead {
public:
    OneOffRead(OperationContext* opCtx, const Timestamp& ts) : _opCtx(opCtx) {
        _opCtx->recoveryUnit()->abandonSnapshot();
        if (ts.isNull()) {
            _opCtx->recoveryUnit()->setTimestampReadSource(RecoveryUnit::ReadSource::kNoTimestamp);
        } else {
            _opCtx->recoveryUnit()->setTimestampReadSource(RecoveryUnit::ReadSource::kProvided, ts);
        }
    }

    ~OneOffRead() {
        _opCtx->recoveryUnit()->abandonSnapshot();
        _opCtx->recoveryUnit()->setTimestampReadSource(RecoveryUnit::ReadSource::kNoTimestamp);
    }

private:
    OperationContext* _opCtx;
};

/**
 * Observed problems using ShardingMongodTestFixture:
 *
 * - Does not mix with dbtest. Both will initialize a ServiceContext.
 * - By default uses ephemeralForTest. These tests require a storage engine that supports majority
 *   reads.
 * - When run as a unittest (and using WT), the fixture initializes the storage engine for each test
 *   that is run. WT specifically installs a ServerStatusSection. The server status code asserts
 *   that a section is never added after a `serverStatus` command is run. Tests defined in
 *   `migration_manager_test` (part of the `db_s_config_server_test` unittest binary) call a
 *   serverStatus triggerring this assertion.
 */
class ReshardingTest : public unittest::Test {
public:
    ServiceContext::UniqueOperationContext _opCtxRaii = cc().makeOperationContext();
    OperationContext* _opCtx = _opCtxRaii.get();
    ServiceContext* _svcCtx = _opCtx->getServiceContext();
    VectorClockMutable* _clock = VectorClockMutable::get(_opCtx);

    ReshardingTest() {
        repl::ReplSettings replSettings;
        replSettings.setOplogSizeBytes(100 * 1024 * 1024);
        replSettings.setReplSetString("rs0");
        setGlobalReplSettings(replSettings);

        auto replCoordinatorMock =
            std::make_unique<repl::ReplicationCoordinatorMock>(_svcCtx, replSettings);
        replCoordinatorMock->alwaysAllowWrites(true);
        repl::ReplicationCoordinator::set(_svcCtx, std::move(replCoordinatorMock));
        repl::StorageInterface::set(_svcCtx, std::make_unique<repl::StorageInterfaceImpl>());
        repl::ReplicationProcess::set(
            _svcCtx,
            std::make_unique<repl::ReplicationProcess>(
                repl::StorageInterface::get(_svcCtx),
                std::make_unique<repl::ReplicationConsistencyMarkersMock>(),
                std::make_unique<repl::ReplicationRecoveryMock>()));

        // Since the Client object persists across tests, even though the global
        // ReplicationCoordinator does not, we need to clear the last op associated with the client
        // to avoid the invariant in ReplClientInfo::setLastOp that the optime only goes forward.
        repl::ReplClientInfo::forClient(_opCtx->getClient()).clearLastOp_forTest();

        auto opObsRegistry = std::make_unique<OpObserverRegistry>();
        opObsRegistry->addObserver(std::make_unique<OpObserverImpl>());
        _opCtx->getServiceContext()->setOpObserver(std::move(opObsRegistry));

        repl::setOplogCollectionName(_svcCtx);
        repl::createOplog(_opCtx);

        _clock->tickClusterTimeTo(LogicalTime(Timestamp(1, 0)));
    }

    ~ReshardingTest() {
        try {
            reset(NamespaceString("local.oplog.rs"));
        } catch (...) {
            FAIL("Exception while cleaning up test");
        }
    }


    /**
     * Walking on ice: resetting the ReplicationCoordinator destroys the underlying
     * `DropPendingCollectionReaper`. Use a truncate/dropAllIndexes to clean out a collection
     * without actually dropping it.
     */
    void reset(NamespaceString nss) const {
        ::mongo::writeConflictRetry(_opCtx, "deleteAll", nss.ns(), [&] {
            _opCtx->recoveryUnit()->setTimestampReadSource(RecoveryUnit::ReadSource::kNoTimestamp);
            AutoGetCollection collRaii(_opCtx, nss, LockMode::MODE_X);

            if (collRaii) {
                WriteUnitOfWork wunit(_opCtx);
                invariant(collRaii.getWritableCollection()->truncate(_opCtx).isOK());
                if (_opCtx->recoveryUnit()->getCommitTimestamp().isNull()) {
                    ASSERT_OK(_opCtx->recoveryUnit()->setTimestamp(Timestamp(1, 1)));
                }
                collRaii.getWritableCollection()->getIndexCatalog()->dropAllIndexes(_opCtx, false);
                wunit.commit();
                return;
            }

            AutoGetOrCreateDb dbRaii(_opCtx, nss.db(), LockMode::MODE_X);
            WriteUnitOfWork wunit(_opCtx);
            if (_opCtx->recoveryUnit()->getCommitTimestamp().isNull()) {
                ASSERT_OK(_opCtx->recoveryUnit()->setTimestamp(Timestamp(1, 1)));
            }
            invariant(dbRaii.getDb()->createCollection(_opCtx, nss));
            wunit.commit();
        });
    }

    void insertDocument(const CollectionPtr& coll, const InsertStatement& stmt) {
        // Insert some documents.
        OpDebug* const nullOpDebug = nullptr;
        const bool fromMigrate = false;
        ASSERT_OK(coll->insertDocument(_opCtx, stmt, nullOpDebug, fromMigrate));
    }

    BSONObj queryCollection(NamespaceString nss, const BSONObj& query) {
        BSONObj ret;
        ASSERT_TRUE(Helpers::findOne(
            _opCtx, AutoGetCollectionForRead(_opCtx, nss).getCollection(), query, ret))
            << "Query: " << query;
        return ret;
    }

    BSONObj queryOplog(const BSONObj& query) {
        OneOffRead oor(_opCtx, Timestamp::min());
        return queryCollection(NamespaceString::kRsOplogNamespace, query);
    }

    repl::OpTime getLastApplied() {
        return repl::ReplicationCoordinator::get(_opCtx)->getMyLastAppliedOpTime();
    }

    boost::intrusive_ptr<ExpressionContextForTest> createExpressionContext() {
        NamespaceString slimNss =
            NamespaceString("local.system.resharding.slimOplogForGraphLookup");

        boost::intrusive_ptr<ExpressionContextForTest> expCtx(
            new ExpressionContextForTest(_opCtx, NamespaceString::kRsOplogNamespace));
        expCtx->setResolvedNamespace(NamespaceString::kRsOplogNamespace,
                                     {NamespaceString::kRsOplogNamespace, {}});
        expCtx->setResolvedNamespace(slimNss,
                                     {slimNss, std::vector<BSONObj>{getSlimOplogPipeline()}});
        return expCtx;
    }

    int itcount(NamespaceString nss) {
        OneOffRead oof(_opCtx, Timestamp::min());
        AutoGetCollectionForRead autoColl(_opCtx, nss);
        auto cursor = autoColl.getCollection()->getCursor(_opCtx);

        int ret = 0;
        while (auto rec = cursor->next()) {
            ++ret;
        }

        return ret;
    }
};

TEST_F(ReshardingTest, RunFetchIteration) {
    const NamespaceString outputCollectionNss("dbtests.outputCollection");
    reset(outputCollectionNss);
    const NamespaceString dataCollectionNss("dbtests.runFetchIteration");
    reset(dataCollectionNss);

    AutoGetCollection dataColl(_opCtx, dataCollectionNss, LockMode::MODE_IX);

    // Set a failpoint to tack a `destinedRecipient` onto oplog entries.
    setGlobalFailPoint("addDestinedRecipient",
                       BSON("mode"
                            << "alwaysOn"
                            << "data"
                            << BSON("destinedRecipient"
                                    << "shard1")));

    // Insert five documents. Advance the majority point. Insert five more documents.
    const std::int32_t docsToInsert = 5;
    {
        WriteUnitOfWork wuow(_opCtx);
        for (std::int32_t num = 0; num < docsToInsert; ++num) {
            insertDocument(dataColl.getCollection(),
                           InsertStatement(BSON("_id" << num << "a" << num)));
        }
        wuow.commit();
    }

    repl::StorageInterface::get(_opCtx)->waitForAllEarlierOplogWritesToBeVisible(_opCtx);
    const Timestamp firstFiveLastApplied = getLastApplied().getTimestamp();
    _svcCtx->getStorageEngine()->getSnapshotManager()->setCommittedSnapshot(firstFiveLastApplied);
    {
        WriteUnitOfWork wuow(_opCtx);
        for (std::int32_t num = docsToInsert; num < 2 * docsToInsert; ++num) {
            insertDocument(dataColl.getCollection(),
                           InsertStatement(BSON("_id" << num << "a" << num)));
        }
        wuow.commit();
    }

    // Disable the failpoint.
    setGlobalFailPoint("addDestinedRecipient",
                       BSON("mode"
                            << "off"));

    repl::StorageInterface::get(_opCtx)->waitForAllEarlierOplogWritesToBeVisible(_opCtx);
    const Timestamp latestLastApplied = getLastApplied().getTimestamp();

    BSONObj firstOplog = queryOplog(BSONObj());
    Timestamp firstTimestamp = firstOplog["ts"].timestamp();
    std::cout << "First oplog: " << firstOplog.toString()
              << " Timestamp: " << firstTimestamp.toString() << std::endl;

    // The first call to `iterate` should return the first five inserts and return a
    // `ReshardingDonorOplogId` matching the last applied of those five inserts.
    ReshardingOplogFetcher fetcher;
    DBDirectClient client(_opCtx);
    StatusWith<ReshardingDonorOplogId> ret = fetcher.iterate(_opCtx,
                                                             &client,
                                                             createExpressionContext(),
                                                             {firstTimestamp, firstTimestamp},
                                                             dataColl->uuid(),
                                                             {"shard1"},
                                                             true,
                                                             outputCollectionNss);
    ReshardingDonorOplogId donorOplodId = unittest::assertGet(ret);
    // +1 because of the create collection oplog entry.
    ASSERT_EQ(docsToInsert + 1, itcount(outputCollectionNss));
    ASSERT_EQ(firstFiveLastApplied, donorOplodId.getClusterTime());
    ASSERT_EQ(firstFiveLastApplied, donorOplodId.getTs());

    // Advance the committed snapshot. A second `iterate` should return the second batch of five
    // inserts.
    _svcCtx->getStorageEngine()->getSnapshotManager()->setCommittedSnapshot(
        getLastApplied().getTimestamp());

    ret = fetcher.iterate(_opCtx,
                          &client,
                          createExpressionContext(),
                          {firstFiveLastApplied, firstFiveLastApplied},
                          dataColl->uuid(),
                          {"shard1"},
                          true,
                          outputCollectionNss);

    donorOplodId = unittest::assertGet(ret);
    // Two batches of five inserts + 1 entry for the create collection oplog entry.
    ASSERT_EQ((2 * docsToInsert) + 1, itcount(outputCollectionNss));
    ASSERT_EQ(latestLastApplied, donorOplodId.getClusterTime());
    ASSERT_EQ(latestLastApplied, donorOplodId.getTs());
}
}  // namespace
}  // namespace mongo