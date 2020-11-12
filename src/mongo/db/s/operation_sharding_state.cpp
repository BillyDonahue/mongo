/**
 *    Copyright (C) 2018-present MongoDB, Inc.
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

#include "mongo/platform/basic.h"

#include "mongo/db/s/operation_sharding_state.h"

#include "mongo/db/operation_context.h"

namespace mongo {
namespace {

const OperationContext::Decoration<OperationShardingState> shardingMetadataDecoration =
    OperationContext::declareDecoration<OperationShardingState>();

// Max time to wait for the migration critical section to complete
const Milliseconds kMaxWaitForMigrationCriticalSection = Minutes(5);

// Max time to wait for the movePrimary critical section to complete
const Milliseconds kMaxWaitForMovePrimaryCriticalSection = Minutes(5);

// The name of the field in which the client attaches its database version.
constexpr auto kDbVersionField = "databaseVersion"_sd;
}  // namespace

OperationShardingState::OperationShardingState() = default;

OperationShardingState::~OperationShardingState() {
    invariant(!_shardingOperationFailedStatus);
}

OperationShardingState& OperationShardingState::get(OperationContext* opCtx) {
    return shardingMetadataDecoration(opCtx);
}

bool OperationShardingState::isOperationVersioned(OperationContext* opCtx) {
    return !(get(opCtx)._shardVersions.empty());
}

void OperationShardingState::setAllowImplicitCollectionCreation(
    const BSONElement& allowImplicitCollectionCreationElem) {
    if (!allowImplicitCollectionCreationElem.eoo()) {
        _allowImplicitCollectionCreation = allowImplicitCollectionCreationElem.Bool();
    } else {
        _allowImplicitCollectionCreation = true;
    }
}

bool OperationShardingState::allowImplicitCollectionCreation() const {
    return _allowImplicitCollectionCreation;
}

void OperationShardingState::initializeClientRoutingVersionsFromCommand(NamespaceString nss,
                                                                        const BSONObj& cmdObj) {
    std::optional<ChunkVersion> shardVersion;
    std::optional<DatabaseVersion> dbVersion;
    const auto shardVersionElem = cmdObj.getField(ChunkVersion::kShardVersionField);
    if (!shardVersionElem.eoo()) {
        shardVersion = uassertStatusOK(ChunkVersion::parseFromCommand(cmdObj));
    }

    const auto dbVersionElem = cmdObj.getField(kDbVersionField);
    if (!dbVersionElem.eoo()) {
        uassert(ErrorCodes::BadValue,
                str::stream() << "expected databaseVersion element to be an object, got "
                              << dbVersionElem,
                dbVersionElem.type() == BSONType::Object);

        dbVersion = DatabaseVersion::parse(IDLParserErrorContext("initializeClientRoutingVersions"),
                                           dbVersionElem.Obj());
    }

    initializeClientRoutingVersions(nss, shardVersion, dbVersion);
}

void OperationShardingState::initializeClientRoutingVersions(
    NamespaceString nss,
    const std::optional<ChunkVersion>& shardVersion,
    const std::optional<DatabaseVersion>& dbVersion) {
    if (shardVersion) {
        invariant(_shardVersionsChecked.find(nss.ns()) == _shardVersionsChecked.end(), nss.ns());
        _shardVersions[nss.ns()] = *shardVersion;
    }
    if (dbVersion) {
        invariant(_databaseVersions.find(nss.db()) == _databaseVersions.end());
        _databaseVersions[nss.db()] = *dbVersion;
    }
}

bool OperationShardingState::hasShardVersion(const NamespaceString& nss) const {
    return _shardVersions.find(nss.ns()) != _shardVersions.end();
}

std::optional<ChunkVersion> OperationShardingState::getShardVersion(const NamespaceString& nss) {
    _shardVersionsChecked.insert(nss.ns());
    const auto it = _shardVersions.find(nss.ns());
    if (it != _shardVersions.end()) {
        return it->second;
    }

    return std::nullopt;
}

bool OperationShardingState::hasDbVersion() const {
    return !_databaseVersions.empty();
}

std::optional<DatabaseVersion> OperationShardingState::getDbVersion(
    const StringData dbName) const {
    const auto it = _databaseVersions.find(dbName);
    if (it == _databaseVersions.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool OperationShardingState::waitForMigrationCriticalSectionSignal(OperationContext* opCtx) {
    // Must not block while holding a lock
    invariant(!opCtx->lockState()->isLocked());

    if (_migrationCriticalSectionSignal) {
        auto deadline = opCtx->getServiceContext()->getFastClockSource()->now() +
            std::min(opCtx->getRemainingMaxTimeMillis(), kMaxWaitForMigrationCriticalSection);

        opCtx->runWithDeadline(deadline, ErrorCodes::ExceededTimeLimit, [&] {
            _migrationCriticalSectionSignal->wait(opCtx);
        });

        _migrationCriticalSectionSignal = std::nullopt;
        return true;
    }

    return false;
}

void OperationShardingState::setMigrationCriticalSectionSignal(
    std::optional<SharedSemiFuture<void>> critSecSignal) {
    invariant(critSecSignal);
    _migrationCriticalSectionSignal = std::move(critSecSignal);
}

bool OperationShardingState::waitForMovePrimaryCriticalSectionSignal(OperationContext* opCtx) {
    // Must not block while holding a lock
    invariant(!opCtx->lockState()->isLocked());

    if (_movePrimaryCriticalSectionSignal) {
        auto deadline = opCtx->getServiceContext()->getFastClockSource()->now() +
            std::min(opCtx->getRemainingMaxTimeMillis(), kMaxWaitForMovePrimaryCriticalSection);

        opCtx->runWithDeadline(deadline, ErrorCodes::ExceededTimeLimit, [&] {
            _movePrimaryCriticalSectionSignal->wait(opCtx);
        });

        _movePrimaryCriticalSectionSignal = std::nullopt;
        return true;
    }

    return false;
}

void OperationShardingState::setMovePrimaryCriticalSectionSignal(
    std::optional<SharedSemiFuture<void>> critSecSignal) {
    invariant(critSecSignal);
    _movePrimaryCriticalSectionSignal = std::move(critSecSignal);
}

void OperationShardingState::setShardingOperationFailedStatus(const Status& status) {
    invariant(!_shardingOperationFailedStatus);
    _shardingOperationFailedStatus = std::move(status);
}

std::optional<Status> OperationShardingState::resetShardingOperationFailedStatus() {
    if (!_shardingOperationFailedStatus) {
        return std::nullopt;
    }
    Status failedStatus = Status(*_shardingOperationFailedStatus);
    _shardingOperationFailedStatus = std::nullopt;
    return failedStatus;
}

}  // namespace mongo
