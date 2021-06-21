/**
 * This test simulates workflows for adding a new node and resyncing a node recommended by docs.
 */
(function() {
"use strict";

load('jstests/replsets/rslib.js');  // waitForState.

const testName = TestData.testName;
const rst = new ReplSetTest({
    name: testName,
    nodes: [{}, {rsConfig: {priority: 0}}, {rsConfig: {priority: 0}}],
    useBridge: true,
    // We shorten the election timeout period so the tests with an unhealthy set run and recover
    // faster.
    settings: {electionTimeoutMillis: 2000, heartbeatIntervalMillis: 400}
});

const verbose = (s) => jsTestLog("[verbose] " + s);

rst.startSet();
rst.initiate();
// Add some data.
const primary = rst.getPrimary();
const testDb = primary.getDB("test");
assert.commandWorked(testDb[testName].insert([{a: 1}, {b: 2}, {c: 3}]));
rst.awaitReplication();

jsTestLog("Test adding a node with initial sync with two secondaries unreachable.");

let config = rst.getReplSetConfigFromNode();
disconnectSecondaries(rst, 2);

verbose("Wait for the set to become unhealthy.");
rst.waitForState(primary, ReplSetTest.State.SECONDARY);

verbose("Add a new, voting node.");
const newNode = rst.add({rsConfig: {priority: 0}});
// The second disconnect ensures we can't reach the new node from the 'down' nodes.
disconnectSecondaries(rst, 2);
const newConfig = rst.getReplSetConfig();
config.members = newConfig.members;
config.version += 1;

verbose("Reconfiguring set to add node.");
assert.commandWorked(primary.adminCommand(
    {replSetReconfig: config, maxTimeMS: ReplSetTest.kDefaultTimeoutMS, force: true}));

verbose("Waiting for node to sync.");
rst.waitForState(newNode, ReplSetTest.State.SECONDARY);

verbose("Make sure the set is still consistent after adding the node.");
reconnectSecondaries(rst);

verbose("If we were in a majority-down scenario, wait for the primary to be re-elected.");
assert.soon(() => primary == rst.getPrimary());
verbose("Wait for the automatic reconfig to complete.");
rst.waitForAllNewlyAddedRemovals();

rst.checkOplogs();
rst.checkReplicatedDataHashes();

// Remove our extra node.
rst.stop(newNode);
rst.remove(newNode);
rst.reInitiate();


rst.stopSet();
})();
