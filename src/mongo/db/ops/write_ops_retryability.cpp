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

#include "mongo/db/ops/write_ops_retryability.h"

#include "mongo/db/dbdirectclient.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/ops/write_ops_gen.h"
#include "mongo/logv2/redaction.h"

namespace mongo {
namespace {

/**
 * Validates that the request is retry-compatible with the operation that occurred.
 * In the case of nested oplog entry where the correct links are in the top level
 * oplog, oplogWithCorrectLinks can be used to specify the outer oplog.
 */
void validateFindAndModifyRetryability(const write_ops::FindAndModifyCommand& request,
                                       const repl::OplogEntry& oplogEntry,
                                       const repl::OplogEntry& oplogWithCorrectLinks) {
    auto opType = oplogEntry.getOpType();
    auto ts = oplogEntry.getTimestamp();

    if (opType == repl::OpTypeEnum::kDelete) {
        uassert(
            40606,
            str::stream() << "findAndModify retry request: " << redact(request.toBSON({}))
                          << " is not compatible with previous write in the transaction of type: "
                          << OpType_serializer(oplogEntry.getOpType()) << ", oplogTs: "
                          << ts.toString() << ", oplog: " << redact(oplogEntry.toBSONForLogging()),
            request.getRemove().value_or(false));
        uassert(40607,
                str::stream() << "No pre-image available for findAndModify retry request:"
                              << redact(request.toBSON({})),
                oplogWithCorrectLinks.getPreImageOpTime());
    } else if (opType == repl::OpTypeEnum::kInsert) {
        uassert(
            40608,
            str::stream() << "findAndModify retry request: " << redact(request.toBSON({}))
                          << " is not compatible with previous write in the transaction of type: "
                          << OpType_serializer(oplogEntry.getOpType()) << ", oplogTs: "
                          << ts.toString() << ", oplog: " << redact(oplogEntry.toBSONForLogging()),
            request.getUpsert().value_or(false));
    } else {
        uassert(
            40609,
            str::stream() << "findAndModify retry request: " << redact(request.toBSON({}))
                          << " is not compatible with previous write in the transaction of type: "
                          << OpType_serializer(oplogEntry.getOpType()) << ", oplogTs: "
                          << ts.toString() << ", oplog: " << redact(oplogEntry.toBSONForLogging()),
            opType == repl::OpTypeEnum::kUpdate);

        if (request.getNew().value_or(false)) {
            uassert(40611,
                    str::stream() << "findAndModify retry request: " << redact(request.toBSON({}))
                                  << " wants the document after update returned, but only before "
                                     "update document is stored, oplogTs: "
                                  << ts.toString()
                                  << ", oplog: " << redact(oplogEntry.toBSONForLogging()),
                    oplogWithCorrectLinks.getPostImageOpTime());
        } else {
            uassert(40612,
                    str::stream() << "findAndModify retry request: " << redact(request.toBSON({}))
                                  << " wants the document before update returned, but only after "
                                     "update document is stored, oplogTs: "
                                  << ts.toString()
                                  << ", oplog: " << redact(oplogEntry.toBSONForLogging()),
                    oplogWithCorrectLinks.getPreImageOpTime());
        }
    }
}

/**
 * Extracts either the pre or post image (cannot be both) of the findAndModify operation from the
 * oplog.
 */
BSONObj extractPreOrPostImage(OperationContext* opCtx, const repl::OplogEntry& oplog) {
    invariant(oplog.getPreImageOpTime() || oplog.getPostImageOpTime());
    auto opTime = oplog.getPreImageOpTime() ? oplog.getPreImageOpTime().value()
                                            : oplog.getPostImageOpTime().value();

    DBDirectClient client(opCtx);
    auto oplogDoc =
        client.findOne(NamespaceString::kRsOplogNamespace.ns(), opTime.asQuery(), nullptr);

    uassert(40613,
            str::stream() << "oplog no longer contains the complete write history of this "
                             "transaction, log with opTime "
                          << opTime.toString() << " cannot be found",
            !oplogDoc.isEmpty());

    auto oplogEntry = uassertStatusOK(repl::OplogEntry::parse(oplogDoc));
    return oplogEntry.getObject().getOwned();
}

/**
 * Extracts the findAndModify result by inspecting the oplog entries that were generated by a
 * previous execution of the command. In the case of nested oplog entry where the correct links
 * are in the top level oplog, oplogWithCorrectLinks can be used to specify the outer oplog.
 */
write_ops::FindAndModifyReply parseOplogEntryForFindAndModify(
    OperationContext* opCtx,
    const write_ops::FindAndModifyCommand& request,
    const repl::OplogEntry& oplogEntry,
    const repl::OplogEntry& oplogWithCorrectLinks) {
    validateFindAndModifyRetryability(request, oplogEntry, oplogWithCorrectLinks);

    write_ops::FindAndModifyReply result;
    write_ops::FindAndModifyLastError lastError;
    lastError.setNumDocs(1);

    switch (oplogEntry.getOpType()) {
        case repl::OpTypeEnum::kDelete:
            result.setValue(extractPreOrPostImage(opCtx, oplogWithCorrectLinks));
            break;
        case repl::OpTypeEnum::kUpdate:
            lastError.setUpdatedExisting(true);
            result.setValue(extractPreOrPostImage(opCtx, oplogWithCorrectLinks));
            break;
        case repl::OpTypeEnum::kInsert: {
            lastError.setUpdatedExisting(false);

            auto owned = oplogEntry.getObject().getOwned();
            auto id = owned.getField("_id");
            if (id) {
                lastError.setUpserted(IDLAnyTypeOwned(id, owned));
            }

            if (request.getNew().value_or(false)) {
                result.setValue(oplogEntry.getObject().getOwned());
            }
            break;
        }
        default:
            MONGO_UNREACHABLE;
    }
    result.setLastErrorObject(std::move(lastError));
    return result;
}

repl::OplogEntry getInnerNestedOplogEntry(const repl::OplogEntry& entry) {
    uassert(40635,
            str::stream() << "expected nested oplog entry with ts: "
                          << entry.getTimestamp().toString()
                          << " to have o2 field: " << redact(entry.toBSONForLogging()),
            entry.getObject2());
    return uassertStatusOK(repl::OplogEntry::parse(*entry.getObject2()));
}

}  // namespace

SingleWriteResult parseOplogEntryForUpdate(const repl::OplogEntry& entry) {
    SingleWriteResult res;
    // Upserts are stored as inserts.
    if (entry.getOpType() == repl::OpTypeEnum::kInsert) {
        res.setN(1);
        res.setNModified(0);

        BSONObjBuilder upserted;
        upserted.append(entry.getObject()["_id"]);
        res.setUpsertedId(upserted.obj());
    } else if (entry.getOpType() == repl::OpTypeEnum::kUpdate) {
        res.setN(1);
        res.setNModified(1);
    } else if (entry.getOpType() == repl::OpTypeEnum::kNoop) {
        return parseOplogEntryForUpdate(getInnerNestedOplogEntry(entry));
    } else {
        uasserted(40638,
                  str::stream() << "update retry request is not compatible with previous write in "
                                   "the transaction of type: "
                                << OpType_serializer(entry.getOpType())
                                << ", oplogTs: " << entry.getTimestamp().toString()
                                << ", oplog: " << redact(entry.toBSONForLogging()));
    }

    return res;
}

write_ops::FindAndModifyReply parseOplogEntryForFindAndModify(
    OperationContext* opCtx,
    const write_ops::FindAndModifyCommand& request,
    const repl::OplogEntry& oplogEntry) {
    // Migrated op case.
    if (oplogEntry.getOpType() == repl::OpTypeEnum::kNoop) {
        return parseOplogEntryForFindAndModify(
            opCtx, request, getInnerNestedOplogEntry(oplogEntry), oplogEntry);
    }

    return parseOplogEntryForFindAndModify(opCtx, request, oplogEntry, oplogEntry);
}

}  // namespace mongo
