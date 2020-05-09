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

#include "mongo/db/update/modifier_table.h"

#include <memory>
#include <string>
#include <utility>

#include "mongo/base/init.h"
#include "mongo/base/simple_string_data_comparator.h"
#include "mongo/base/status.h"
#include "mongo/db/update/addtoset_node.h"
#include "mongo/db/update/arithmetic_node.h"
#include "mongo/db/update/bit_node.h"
#include "mongo/db/update/compare_node.h"
#include "mongo/db/update/conflict_placeholder_node.h"
#include "mongo/db/update/current_date_node.h"
#include "mongo/db/update/pop_node.h"
#include "mongo/db/update/pull_node.h"
#include "mongo/db/update/pullall_node.h"
#include "mongo/db/update/push_node.h"
#include "mongo/db/update/rename_node.h"
#include "mongo/db/update/set_node.h"
#include "mongo/db/update/unset_node.h"
#include "mongo/util/static_immortal.h"
#include "mongo/util/string_map.h"

namespace mongo {

namespace modifiertable {

ModifierType getType(StringData typeStr) {
    static const StaticImmortal map =
        StringDataMap<ModifierType>{{"$addToSet", MOD_ADD_TO_SET},
                                    {"$bit", MOD_BIT},
                                    {"$currentDate", MOD_CURRENTDATE},
                                    {"$inc", MOD_INC},
                                    {"$max", MOD_MAX},
                                    {"$min", MOD_MIN},
                                    {"$mul", MOD_MUL},
                                    {"$pop", MOD_POP},
                                    {"$pull", MOD_PULL},
                                    {"$pullAll", MOD_PULL_ALL},
                                    {"$push", MOD_PUSH},
                                    {"$set", MOD_SET},
                                    {"$setOnInsert", MOD_SET_ON_INSERT},
                                    {"$rename", MOD_RENAME},
                                    {"$unset", MOD_UNSET}};
    if (auto it = map->find(typeStr); it != map->end()) {
        return it->second;
    }
    return MOD_UNKNOWN;
}

std::unique_ptr<UpdateLeafNode> makeUpdateLeafNode(ModifierType modType) {
    switch (modType) {
        case MOD_ADD_TO_SET:
            return std::make_unique<AddToSetNode>();
        case MOD_BIT:
            return std::make_unique<BitNode>();
        case MOD_CONFLICT_PLACEHOLDER:
            return std::make_unique<ConflictPlaceholderNode>();
        case MOD_CURRENTDATE:
            return std::make_unique<CurrentDateNode>();
        case MOD_INC:
            return std::make_unique<ArithmeticNode>(ArithmeticNode::ArithmeticOp::kAdd);
        case MOD_MAX:
            return std::make_unique<CompareNode>(CompareNode::CompareMode::kMax);
        case MOD_MIN:
            return std::make_unique<CompareNode>(CompareNode::CompareMode::kMin);
        case MOD_MUL:
            return std::make_unique<ArithmeticNode>(ArithmeticNode::ArithmeticOp::kMultiply);
        case MOD_POP:
            return std::make_unique<PopNode>();
        case MOD_PULL:
            return std::make_unique<PullNode>();
        case MOD_PULL_ALL:
            return std::make_unique<PullAllNode>();
        case MOD_PUSH:
            return std::make_unique<PushNode>();
        case MOD_RENAME:
            return std::make_unique<RenameNode>();
        case MOD_SET:
            return std::make_unique<SetNode>();
        case MOD_SET_ON_INSERT:
            return std::make_unique<SetNode>(UpdateNode::Context::kInsertOnly);
        case MOD_UNSET:
            return std::make_unique<UnsetNode>();
        default:
            return nullptr;
    }
}

}  // namespace modifiertable
}  // namespace mongo
