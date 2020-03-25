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

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kCommand

#include "mongo/platform/basic.h"

#include "mongo/db/commands/parameters.h"

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <set>
#include <type_traits>
#include <vector>

#include "mongo/base/status_with.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/bsontypes.h"
#include "mongo/bson/json.h"
#include "mongo/client/replica_set_monitor.h"
#include "mongo/config.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/command_generic_argument.h"
#include "mongo/db/commands.h"
#include "mongo/db/commands/parameters_gen.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/logv2/log.h"
#include "mongo/logv2/log_component.h"
#include "mongo/util/assert_util.h"

namespace mongo {
namespace {

using namespace fmt::literals;
using logv2::LogComponent;
using logv2::LogSeverity;

namespace log_component_verbosity_detail {
using server_parameter_detail::LogComponentSetting;

class LogComponentIterator : public boost::iterator_facade<LogComponentIterator,
                                                           const LogComponent,
                                                           boost::forward_traversal_tag> {
public:
    LogComponentIterator() : LogComponentIterator{LogComponent::kNumLogComponents} {}
    explicit LogComponentIterator(LogComponent c) : _c{c} {}

private:
    friend class boost::iterator_core_access;
    void increment() {
        using I = std::underlying_type_t<LogComponent::Value>;
        I i = static_cast<LogComponent::Value>(_c);
        ++i;
        _c = static_cast<LogComponent::Value>(i);
    }
    const LogComponent& dereference() const {
        return _c;
    }
    bool equal(const LogComponentIterator& other) const {
        return _c == *other;
    }

    LogComponent _c;
};

auto allLogComponents() {
    return std::make_pair(LogComponentIterator{LogComponent::kDefault}, LogComponentIterator{});
}

void _appendVerbosityObject(LogComponent component, BSONObjBuilder* out) {
    auto& gs = logv2::LogManager::global().getGlobalSettings();
    int severity =
        gs.hasMinimumLogSeverity(component) ? gs.getMinimumLogSeverity(component).toInt() : -1;
    out->append("verbosity", severity);
    for (auto [b, e] = allLogComponents(); b != e; ++b) {
        if (b->parent() == component) {
            auto childObj = BSONObjBuilder(out->subobjStart(b->getShortName()));
            _appendVerbosityObject(*b, &childObj);
        }
    }
}

/**
 * Returns current settings as a BSON document.
 * Every component is in an object that contains a key for its "verbosity", and then the
 * shortName of each of its child components is mapped to that child's subobject. The
 * kDefault component is the implicit root, and doesn't appear by name.
 */
BSONObj _get() {
    BSONObjBuilder doc;
    _appendVerbosityObject(LogComponent::kDefault, &doc);
    return doc.obj();
}

/**
 * Parses instructions for modifying component log levels from "settings".
 * Result elements each describe how to change a particular log component's verbosity level.
 * Throws a DBException if parsing fails.
 */
std::vector<LogComponentSetting> _parseLogComponentSettings(const BSONObj& settings) {
    std::vector<LogComponentSetting> levelsToSet;

    auto onMatchObj = [&](LogComponent c, const BSONObj& obj) {
        // The configuring subobject can have a "verbosity" field mapped to int.
        // It's still okay if it doesn't have one at all.
        auto elem = obj.getField("verbosity");
        if (elem.eoo())
            return;
        uassert(ErrorCodes::BadValue,
                "Expected {}.verbosity to be a number, but found {}"_format(
                c.getDottedName(), typeName(elem.type())), elem.isNumber());
        levelsToSet.push_back({c, elem.numberInt()});
    };

    for (auto [b, e] = allLogComponents(); b != e; ++b) {
        if (*b == LogComponent::kDefault) {
            // As a special case, kDefault is represented by the root object.
            onMatchObj(*b, settings);
            continue;
        }

        const std::string dottedStore = b->getDottedName();
        BSONObj node = settings;
        for (StringData dotted = dottedStore; !dotted.empty(); ) {
            size_t dot = dotted.find('.');
            const StringData pathPart = dotted.substr(0, dot);
            dotted = dotted.substr(dot == std::string::npos ? dotted.size() : dot + 1);
            const BSONElement kid = node.getField(pathPart);
            if (kid.eoo())
                break;  // Full path was not found
            else if (dotted.empty()) {
                // Kid is the final path element
                // It can be mapped to an Object containing a "verbosity" element,
                // or it can be directly mapped to an integer.
                if (kid.type() == Object) {
                    onMatchObj(*b, kid.Obj());
                } else if (kid.isNumber()) {
                    levelsToSet.push_back({*b, kid.numberInt()});
                } else {
                    uasserted(ErrorCodes::BadValue,
                              "Invalid type {} for component {}"_format(
                                  typeName(kid.type()), dottedStore));
                }
                break;
            }
            if (kid.type() != Object)
                break;
            node = kid.Obj();
        }
    }

#if 0
    while (iter.moreWithEOO()) {
        // ...
        if (it == e || it->parent() != parentComponent) {
            uasserted(
                ErrorCodes::BadValue,
                "Invalid component name {}.{}"_format(parentComponent.getDottedName(), shortName));
        }
        // ...
    }
#endif
    return levelsToSet;
}

/**
 * Updates component hierarchy log levels.
 *
 * BSON Format:
 * {
 *     verbosity: 4,  <-- maps to 'default' log component.
 *     componentA: {
 *         verbosity: 2,  <-- sets componentA's log level to 2.
 *         componentB: {
 *             verbosity: 1, <-- sets componentA.componentB's log level to 1.
 *         }
 *         componentC: {
 *             verbosity: -1, <-- clears componentA.componentC's log level so that
 *                                its final loglevel will be inherited from componentA.
 *         }
 *     },
 *     componentD : 3  <-- sets componentD's log level to 3 (alternative to
 *                         subdocument with 'verbosity' field).
 * }
 *
 * For the default component, the log level is read from the top-level
 * "verbosity" field.
 * For non-default components, we look up the element using the component's
 * dotted name. If the "<dotted component name>" field is a number, the log
 * level will be read from the field's value.
 * Otherwise, we assume that the "<dotted component name>" field is an
 * object with a "verbosity" field that holds the log level for the component.
 * The more verbose format with the "verbosity" field is intended to support
 * setting of log levels of both parent and child log components in the same
 * BSON document.
 *
 * Ignore elements in BSON object that do not map to a log component's dotted
 * name.
 */
void _set(const BSONObj& bsonSettings) {
    auto& gs = logv2::LogManager::global().getGlobalSettings();
    for (const auto& [c, l] : _parseLogComponentSettings(bsonSettings))
        if (l < 0)
            gs.clearMinimumLoggedSeverity(c);  // Negative level means to clear.
        else
            gs.setMinimumLoggedSeverity(c, l ? LogSeverity::Debug(l) : LogSeverity::Log());
}

void append(BSONObjBuilder& builder, const std::string& name) {
    builder.append(name, _get());
}

Status set(const BSONElement& newValueElement) {
    if (!newValueElement.isABSONObj()) {
        return Status(ErrorCodes::TypeMismatch,
                      "log component verbosity is not a BSON object: {}"_format(newValueElement));
    }
    try {
        _set(newValueElement.Obj());
        return Status::OK();
    } catch (const DBException& ex) {
        return ex.toStatus();
    }
}

Status setFromString(const std::string& str) {
    try {
        _set(fromjson(str));
        return Status::OK();
    } catch (const DBException& ex) {
        return ex.toStatus();
    }
}

}  // namespace log_component_verbosity_detail

namespace log_level_detail {

void append(BSONObjBuilder& builder, const std::string& name) {
    builder.append(name,
                   logv2::LogManager::global()
                       .getGlobalSettings()
                       .getMinimumLogSeverity(LogComponent::kDefault)
                       .toInt());
}

Status set(const BSONElement& newValueElement) {
    int newValue;
    if (!newValueElement.coerce(&newValue) || newValue < 0)
        return Status(ErrorCodes::BadValue,
                      "Invalid value for logLevel: {}"_format(newValueElement));
    LogSeverity newSeverity = (newValue > 0) ? LogSeverity::Debug(newValue) : LogSeverity::Log();
    auto& gs = logv2::LogManager::global().getGlobalSettings();
    gs.setMinimumLoggedSeverity(logv2::LogComponent::kDefault, newSeverity);
    return Status::OK();
}

Status setFromString(const std::string& strLevel) {
    int newValue;
    Status status = NumberParser{}(strLevel, &newValue);
    if (!status.isOK())
        return status;
    if (newValue < 0)
        return Status(ErrorCodes::BadValue, "Invalid value for logLevel: {}"_format(newValue));
    LogSeverity newSeverity = (newValue > 0) ? LogSeverity::Debug(newValue) : LogSeverity::Log();
    auto& gs = logv2::LogManager::global().getGlobalSettings();
    gs.setMinimumLoggedSeverity(logv2::LogComponent::kDefault, newSeverity);
    return Status::OK();
}

}  // namespace log_level_detail

namespace automation_service_descriptor_detail {

Mutex _mutex;
std::string _value;

void append(BSONObjBuilder& builder, const std::string& name) {
    stdx::lock_guard lock(_mutex);
    if (!_value.empty())
        builder.append(name, _value);
}

Status setFromString(const std::string& str) {
    static constexpr size_t kMaxSize = 64;
    if (str.size() > kMaxSize)
        return {ErrorCodes::Overflow,
                "Value for parameter automationServiceDescriptor"
                " must be no more than {} bytes"_format(kMaxSize)};
    stdx::lock_guard lock(_mutex);
    _value = str;
    return Status::OK();
}

Status set(const BSONElement& newValueElement) {
    if (newValueElement.type() != String)
        return {ErrorCodes::TypeMismatch,
                "Value for parameter automationServiceDescriptor must be of type 'string'"};
    return setFromString(newValueElement.String());
}

}  // namespace automation_service_descriptor_detail

void _appendParameterNames(std::string* help) {
    auto out = std::back_inserter(*help);
    format_to(out, FMT_STRING("supported:\n"));
    for (const auto& [k, v] : ServerParameterSet::getGlobal()->getMap())
        format_to(out, FMT_STRING("  {}\n"), k);
}

class CmdGet : public ErrmsgCommandDeprecated {
public:
    CmdGet() : ErrmsgCommandDeprecated("getParameter") {}

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kAlways;
    }

    bool adminOnly() const override {
        return true;
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }

    void addRequiredPrivileges(const std::string& dbname,
                               const BSONObj& cmdObj,
                               std::vector<Privilege>* out) const override {
        ActionSet actions;
        actions.addAction(ActionType::getParameter);
        out->push_back(Privilege(ResourcePattern::forClusterResource(), actions));
    }

    std::string help() const override {
        std::string h =
            "get administrative option(s)\n"
            "example:\n"
            "{ getParameter:1, notablescan:1 }\n";
        _appendParameterNames(&h);
        h += "{ getParameter:'*' } to get everything\n";
        return h;
    }

    bool errmsgRun(OperationContext* opCtx,
                   const std::string& dbname,
                   const BSONObj& cmdObj,
                   std::string& errmsg,
                   BSONObjBuilder& result) override {
        bool all = *cmdObj.firstElement().valuestrsafe() == '*';

        int before = result.len();

        for (const auto& [name, param] : ServerParameterSet::getGlobal()->getMap()) {
            if (all || cmdObj.hasElement(name.c_str())) {
                param->append(opCtx, result, param->name());
            }
        }

        if (before == result.len()) {
            errmsg = "no option found to get";
            return false;
        }
        return true;
    }
} cmdGet;

class CmdSet : public ErrmsgCommandDeprecated {
public:
    CmdSet() : ErrmsgCommandDeprecated("setParameter") {}

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kAlways;
    }

    bool adminOnly() const override {
        return true;
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }

    void addRequiredPrivileges(const std::string& dbname,
                               const BSONObj& cmdObj,
                               std::vector<Privilege>* out) const override {
        ActionSet actions;
        actions.addAction(ActionType::setParameter);
        out->push_back(Privilege(ResourcePattern::forClusterResource(), actions));
    }

    std::string help() const override {
        std::string h =
            "set administrative option(s)\n"
            "{ setParameter:1, <param>:<value> }\n";
        _appendParameterNames(&h);
        return h;
    }

    bool errmsgRun(OperationContext* opCtx,
                   const std::string& dbname,
                   const BSONObj& cmdObj,
                   std::string& errmsg,
                   BSONObjBuilder& result) override {
        int numSet = 0;
        bool found = false;

        const ServerParameter::Map& parameterMap = ServerParameterSet::getGlobal()->getMap();

        // First check that we aren't setting the same parameter twice and that we actually are
        // setting parameters that we have registered and can change at runtime
        BSONObjIterator parameterCheckIterator(cmdObj);

        // We already know that "setParameter" will be the first element in this object, so skip
        // past that
        parameterCheckIterator.next();

        // Set of all the parameters the user is attempting to change
        std::map<std::string, BSONElement> parametersToSet;

        // Iterate all parameters the user passed in to do the initial validation checks,
        // including verifying that we are not setting the same parameter twice.
        while (parameterCheckIterator.more()) {
            BSONElement parameter = parameterCheckIterator.next();
            std::string parameterName = parameter.fieldName();
            if (isGenericArgument(parameterName))
                continue;

            auto foundParameter = parameterMap.find(parameterName);
            // Check to see if this is actually a valid parameter
            if (foundParameter == parameterMap.end()) {
                errmsg =
                    "attempted to set unrecognized parameter [{}],"
                    " use help:true to see options "_format(parameterName);
                return false;
            }

            // Make sure we are allowed to change this parameter
            if (!foundParameter->second->allowedToChangeAtRuntime()) {
                errmsg = "not allowed to change [{}] at runtime"_format(parameterName);
                return false;
            }

            // Make sure we are only setting this parameter once
            if (auto [pos, ok] = parametersToSet.insert({parameterName, parameter}); !ok) {
                errmsg =
                    "attempted to set parameter [{}] twice in the same setParameter command,"
                    " once to value: [{}], and once to value: [{}]"_format(
                        parameterName, pos->second.toString(false), parameter.toString(false));
                return false;
            }
        }

        // Iterate the parameters that we have confirmed we are setting and actually set them.
        // Not that if setting any one parameter fails, the command will fail, but the user
        // won't see what has been set and what hasn't.  See SERVER-8552.
        for (auto&& [parameterName, parameter] : parametersToSet) {
            auto foundParameter = parameterMap.find(parameterName);
            if (foundParameter == parameterMap.end()) {
                errmsg =
                    "Parameter: {} that was available during our first lookup in the"
                    " registered parameters map is no longer available."_format(parameterName);
                return false;
            }
            auto oldValueObj = [&] {
                BSONObjBuilder bb;
                if (numSet == 0) {
                    foundParameter->second->append(opCtx, bb, "was");
                }
                return bb.obj();
            }();
            auto oldValue = oldValueObj.firstElement();

            if (oldValue) {
                result.append(oldValue);
            }

            try {
                uassertStatusOK(foundParameter->second->set(parameter));
            } catch (const DBException& ex) {
                LOGV2(20496,
                      "error setting parameter {parameterName} to {newValue} errMsg: {error}",
                      "Error setting parameter",
                      "parameterName"_attr = parameterName,
                      "newValue"_attr = redact(parameter.toString(false)),
                      "error"_attr = redact(ex));
                throw;
            }

            if (oldValue) {
                LOGV2(23435,
                      "successfully set parameter {parameterName} to {newValue} (was {oldValue})",
                      "Successfully set parameter",
                      "parameterName"_attr = parameterName,
                      "newValue"_attr = redact(parameter.toString(false)),
                      "oldValue"_attr = redact(oldValue.toString(false)));
            } else {
                LOGV2(23436,
                      "successfully set parameter {parameterName} to {newValue}",
                      "Successfully set parameter",
                      "parameterName"_attr = parameterName,
                      "newValue"_attr = redact(parameter.toString(false)));
            }

            ++numSet;
        }

        if (numSet == 0 && !found) {
            errmsg = "no option found to set, use help:true to see options ";
            return false;
        }

        return true;
    }
} cmdSet;

}  // namespace

void LogLevelServerParameter::append(OperationContext*,
                                     BSONObjBuilder& builder,
                                     const std::string& name) {
    log_level_detail::append(builder, name);
}

Status LogLevelServerParameter::set(const BSONElement& newValueElement) {
    return log_level_detail::set(newValueElement);
}

Status LogLevelServerParameter::setFromString(const std::string& strLevel) {
    return log_level_detail::setFromString(strLevel);
}

void LogComponentVerbosityServerParameter::append(OperationContext*,
                                                  BSONObjBuilder& builder,
                                                  const std::string& name) {
    log_component_verbosity_detail::append(builder, name);
}

Status LogComponentVerbosityServerParameter::set(const BSONElement& newValueElement) {
    return log_component_verbosity_detail::set(newValueElement);
}

Status LogComponentVerbosityServerParameter::setFromString(const std::string& str) {
    return log_component_verbosity_detail::setFromString(str);
}

void AutomationServiceDescriptorServerParameter::append(OperationContext*,
                                                        BSONObjBuilder& builder,
                                                        const std::string& name) {
    automation_service_descriptor_detail::append(builder, name);
}

Status AutomationServiceDescriptorServerParameter::set(const BSONElement& newValueElement) {
    return automation_service_descriptor_detail::set(newValueElement);
}

Status AutomationServiceDescriptorServerParameter::setFromString(const std::string& str) {
    return automation_service_descriptor_detail::setFromString(str);
}

namespace server_parameter_detail {
std::vector<LogComponentSetting> _parseLogComponentSettings(const BSONObj& settings) {
    return log_component_verbosity_detail::_parseLogComponentSettings(settings);
}
}  // namespace server_parameter_detail

}  // namespace mongo
