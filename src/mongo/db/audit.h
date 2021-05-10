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

/**
 * This module describes free functions for logging various operations of interest to a
 * party interested in generating logs of user activity in a MongoDB server instance.
 */

#pragma once

#include "mongo/base/error_codes.h"
#include "mongo/db/auth/privilege.h"
#include "mongo/db/auth/user.h"
#include "mongo/db/ops/write_ops.h"
#include "mongo/rpc/op_msg.h"

namespace mongo {

class AuthorizationSession;
class BSONObj;
class Client;
class NamespaceString;
class OperationContext;
class StringData;
class UserName;

namespace mutablebson {
class Document;
}  // namespace mutablebson

namespace audit {

/**
 * Struct that temporarily stores client information when an audit hook
 * executes on a separate thread with a new Client. In those cases, ImpersonatedClientAttrs
 * can bundle all relevant client attributes necessary for auditing and be safely
 * passed into the new thread, where the new Client will be loaded with the userNames and
 * roleNames stored in ImpersonatedClientAttrs.
 */
struct ImpersonatedClientAttrs {
    std::vector<UserName> userNames;
    std::vector<RoleName> roleNames;

    ImpersonatedClientAttrs() = default;

    ImpersonatedClientAttrs(Client* client);
};

/**
 * Narrow API for the parts of mongo::Command used by the audit library.
 */
class CommandInterface {
public:
    virtual ~CommandInterface() = default;
    virtual std::set<StringData> sensitiveFieldNames() const = 0;
    virtual void snipForLogging(mutablebson::Document* cmdObj) const = 0;
    virtual StringData getName() const = 0;
    virtual NamespaceString ns() const = 0;
    virtual bool redactArgs() const = 0;
};

/**
 * Logs the result of an authentication attempt.
 */
void logAuthentication(Client* client,
                       StringData mechanism,
                       const UserName& user,
                       ErrorCodes::Error result);

//
// Authorization (authz) logging functions.
//
// These functions generate log messages describing the disposition of access control
// checks.
//

/**
 * Logs the result of a command authorization check.
 */
void logCommandAuthzCheck(Client* client,
                          const OpMsgRequest& cmdObj,
                          const CommandInterface& command,
                          ErrorCodes::Error result);

/**
 * Logs the result of an authorization check for an OP_DELETE wire protocol message.
 */
void logDeleteAuthzCheck(Client* client,
                         const NamespaceString& ns,
                         const BSONObj& pattern,
                         ErrorCodes::Error result);

/**
 * Logs the result of an authorization check for an OP_GET_MORE wire protocol message.
 */
void logGetMoreAuthzCheck(Client* client,
                          const NamespaceString& ns,
                          long long cursorId,
                          ErrorCodes::Error result);

/**
 * Logs the result of an authorization check for an OP_INSERT wire protocol message.
 */
void logInsertAuthzCheck(Client* client,
                         const NamespaceString& ns,
                         const BSONObj& insertedObj,
                         ErrorCodes::Error result);

/**
 * Logs the result of an authorization check for an OP_KILL_CURSORS wire protocol message.
 */
void logKillCursorsAuthzCheck(Client* client,
                              const NamespaceString& ns,
                              long long cursorId,
                              ErrorCodes::Error result);

/**
 * Logs the result of an authorization check for an OP_QUERY wire protocol message.
 */
void logQueryAuthzCheck(Client* client,
                        const NamespaceString& ns,
                        const BSONObj& query,
                        ErrorCodes::Error result);

/**
 * Logs the result of an authorization check for an OP_UPDATE wire protocol message.
 */
void logUpdateAuthzCheck(Client* client,
                         const NamespaceString& ns,
                         const BSONObj& query,
                         const write_ops::UpdateModification& update,
                         bool isUpsert,
                         bool isMulti,
                         ErrorCodes::Error result);

/**
 * Logs the result of a createUser command.
 */
void logCreateUser(Client* client,
                   const UserName& username,
                   bool password,
                   const BSONObj* customData,
                   const std::vector<RoleName>& roles,
                   const boost::optional<BSONArray>& restrictions);

/**
 * Logs the result of a dropUser command.
 */
void logDropUser(Client* client, const UserName& username);

/**
 * Logs the result of a dropAllUsersFromDatabase command.
 */
void logDropAllUsersFromDatabase(Client* client, StringData dbname);

/**
 * Logs the result of a updateUser command.
 */
void logUpdateUser(Client* client,
                   const UserName& username,
                   bool password,
                   const BSONObj* customData,
                   const std::vector<RoleName>* roles,
                   const boost::optional<BSONArray>& restrictions);

/**
 * Logs the result of a grantRolesToUser command.
 */
void logGrantRolesToUser(Client* client,
                         const UserName& username,
                         const std::vector<RoleName>& roles);

/**
 * Logs the result of a revokeRolesFromUser command.
 */
void logRevokeRolesFromUser(Client* client,
                            const UserName& username,
                            const std::vector<RoleName>& roles);

/**
 * Logs the result of a createRole command.
 */
void logCreateRole(Client* client,
                   const RoleName& role,
                   const std::vector<RoleName>& roles,
                   const PrivilegeVector& privileges,
                   const boost::optional<BSONArray>& restrictions);

/**
 * Logs the result of a updateRole command.
 */
void logUpdateRole(Client* client,
                   const RoleName& role,
                   const std::vector<RoleName>* roles,
                   const PrivilegeVector* privileges,
                   const boost::optional<BSONArray>& restrictions);

/**
 * Logs the result of a dropRole command.
 */
void logDropRole(Client* client, const RoleName& role);

/**
 * Logs the result of a dropAllRolesForDatabase command.
 */
void logDropAllRolesFromDatabase(Client* client, StringData dbname);

/**
 * Logs the result of a grantRolesToRole command.
 */
void logGrantRolesToRole(Client* client, const RoleName& role, const std::vector<RoleName>& roles);

/**
 * Logs the result of a revokeRolesFromRole command.
 */
void logRevokeRolesFromRole(Client* client,
                            const RoleName& role,
                            const std::vector<RoleName>& roles);

/**
 * Logs the result of a grantPrivilegesToRole command.
 */
void logGrantPrivilegesToRole(Client* client,
                              const RoleName& role,
                              const PrivilegeVector& privileges);

/**
 * Logs the result of a revokePrivilegesFromRole command.
 */
void logRevokePrivilegesFromRole(Client* client,
                                 const RoleName& role,
                                 const PrivilegeVector& privileges);

/**
 * Logs the result of a replSet(Re)config command.
 */
void logReplSetReconfig(Client* client, const BSONObj* oldConfig, const BSONObj* newConfig);

/**
 * Logs the result of an ApplicationMessage command.
 */
void logApplicationMessage(Client* client, StringData msg);

/**
 * Logs the options associated with a startup event.
 */
void logStartupOptions(Client* client, const BSONObj& startupOptions);

/**
 * Logs the result of a shutdown command.
 */
void logShutdown(Client* client);

/**
 * Logs the users authenticated to a session before and after a logout command.
 */
void logLogout(Client* client,
               StringData reason,
               const BSONArray& initialUsers,
               const BSONArray& updatedUsers);

/**
 * Logs the result of a createIndex command.
 */
void logCreateIndex(Client* client,
                    const BSONObj* indexSpec,
                    StringData indexname,
                    StringData nsname);

/**
 * Logs the result of a createCollection command.
 */
void logCreateCollection(Client* client, StringData nsname);

/**
 * Logs the result of a createView command.
 */
void logCreateView(Client* client,
                   StringData nsname,
                   StringData viewOn,
                   BSONArray pipeline,
                   ErrorCodes::Error code);

/**
 * Logs the result of an importCollection command.
 */
void logImportCollection(Client* client, StringData nsname);

/**
 * Logs the result of a createDatabase command.
 */
void logCreateDatabase(Client* client, StringData dbname);


/**
 * Logs the result of a dropIndex command.
 */
void logDropIndex(Client* client, StringData indexname, StringData nsname);

/**
 * Logs the result of a dropCollection command on a collection.
 */
void logDropCollection(Client* client, StringData nsname);

/**
 * Logs the result of a dropCollection command on a view.
 */
void logDropView(Client* client,
                 StringData nsname,
                 StringData viewOn,
                 const std::vector<BSONObj>& pipeline,
                 ErrorCodes::Error code);

/**
 * Logs the result of a dropDatabase command.
 */
void logDropDatabase(Client* client, StringData dbname);

/**
 * Logs a collection rename event.
 */
void logRenameCollection(Client* client,
                         const NamespaceString& source,
                         const NamespaceString& target);

/**
 * Logs the result of a enableSharding command.
 */
void logEnableSharding(Client* client, StringData dbname);

/**
 * Logs the result of a addShard command.
 */
void logAddShard(Client* client, StringData name, const std::string& servers, long long maxSize);

/**
 * Logs the result of a removeShard command.
 */
void logRemoveShard(Client* client, StringData shardname);

/**
 * Logs the result of a shardCollection command.
 */
void logShardCollection(Client* client, StringData ns, const BSONObj& keyPattern, bool unique);

/**
 * Logs the result of a refineCollectionShardKey event.
 */
void logRefineCollectionShardKey(Client* client, StringData ns, const BSONObj& keyPattern);

/**
 * Logs an insert of a potentially security sensitive record.
 */
void logInsertOperation(Client* client, const NamespaceString& nss, const BSONObj& doc);

/**
 * Logs an update of a potentially security sensitive record.
 */
void logUpdateOperation(Client* client, const NamespaceString& nss, const BSONObj& doc);

/**
 * Logs a deletion of a potentially security sensitive record.
 */
void logRemoveOperation(Client* client, const NamespaceString& nss, const BSONObj& doc);


}  // namespace audit
}  // namespace mongo
