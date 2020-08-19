/**
 * Tests that resumable index builds complete properly after being interrupted for rollback during
 * the drain writes phase.
 *
 * @tags: [
 *   requires_fcv_47,
 *   requires_majority_read_concern,
 *   requires_persistence,
 * ]
 */
(function() {
"use strict";

load('jstests/replsets/libs/rollback_resumable_index_build.js');

// TODO(SERVER-50775): Re-enable when stepdown issues are fixed in resumable index rollback tests.
if (true) {
    jsTestLog('Skipping test.');
    return;
}

const dbName = "test";
const rollbackStartFailPointName = "hangIndexBuildDuringDrainWritesPhase";
const insertsToBeRolledBack = [{a: 13}, {a: 14}];

const rollbackTest = new RollbackTest(jsTestName());
const coll = rollbackTest.getPrimary().getDB(dbName).getCollection(jsTestName());

assert.commandWorked(coll.insert([{a: 1}, {a: 2}, {a: 3}]));

// Rollback to before the index begins to be built.
RollbackResumableIndexBuildTest.run(rollbackTest,
                                    dbName,
                                    coll.getName(),
                                    {a: 1},
                                    rollbackStartFailPointName,
                                    {iteration: 0},
                                    "hangAfterSettingUpIndexBuildUnlocked",
                                    {},
                                    insertsToBeRolledBack,
                                    [{a: 4}, {a: 5}]);

// Rollback to the collection scan phase.
RollbackResumableIndexBuildTest.run(rollbackTest,
                                    dbName,
                                    coll.getName(),
                                    {a: 1},
                                    rollbackStartFailPointName,
                                    {iteration: 0},
                                    "hangIndexBuildDuringCollectionScanPhaseBeforeInsertion",
                                    {fieldsToMatch: {a: 2}},
                                    insertsToBeRolledBack,
                                    [{a: 6}, {a: 7}]);

// Rollback to the bulk load phase.
RollbackResumableIndexBuildTest.run(rollbackTest,
                                    dbName,
                                    coll.getName(),
                                    {a: 1},
                                    rollbackStartFailPointName,
                                    {iteration: 0},
                                    "hangIndexBuildDuringBulkLoadPhase",
                                    {iteration: 1},
                                    insertsToBeRolledBack,
                                    [{a: 8}, {a: 9}]);

// Rollback to earlier in the drain writes phase. We set maxIndexBuildDrainBatchSize to 1 so that
// the primary can step down between iterations.
assert.commandWorked(
    rollbackTest.getPrimary().adminCommand({setParameter: 1, maxIndexBuildDrainBatchSize: 1}));
RollbackResumableIndexBuildTest.run(rollbackTest,
                                    dbName,
                                    coll.getName(),
                                    {a: 1},
                                    rollbackStartFailPointName,
                                    {iteration: 2},
                                    "hangIndexBuildDuringDrainWritesPhaseSecond",
                                    {iteration: 0},
                                    insertsToBeRolledBack,
                                    [{a: 10}, {a: 11}, {a: 12}]);

rollbackTest.stop();
})();