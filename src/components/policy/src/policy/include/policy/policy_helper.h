/*
 Copyright (c) 2013, Ford Motor Company
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following
 disclaimer in the documentation and/or other materials provided with the
 distribution.

 Neither the name of the Ford Motor Company nor the names of its contributors
 may be used to endorse or promote products derived from this software
 without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SRC_COMPONENTS_POLICY_INCLUDE_POLICY_POLICY_HELPER_H_
#define SRC_COMPONENTS_POLICY_INCLUDE_POLICY_POLICY_HELPER_H_

#include "policy_table_interface_base/functions.h"
#include "utils/shared_ptr.h"
#include "policy/policy_types.h"

namespace policy {
class PolicyManagerImpl;

namespace policy_table = rpc::policy_table_interface_base;

typedef policy_table::Strings::const_iterator StringsConstItr;
typedef policy_table::ApplicationPolicies::const_iterator AppPoliciesConstItr;
typedef policy_table::HmiLevels::const_iterator HMILevelsConstItr;
typedef policy_table::Parameters::const_iterator ParametersConstItr;
typedef policy_table::FunctionalGroupings::const_iterator FuncGroupConstItr;

typedef policy_table::ApplicationPolicies::value_type AppPoliciesValueType;
typedef policy_table::Rpc::value_type RpcValueType;
typedef policy_table::Strings::value_type StringsValueType;

/*
 * @brief Helper struct to compare functional group names
 */
struct CompareGroupName {
  explicit CompareGroupName(const StringsValueType& group_name);
  bool operator()(const StringsValueType& group_name_to_compare) const;
 private:
  const StringsValueType& group_name_;
};

/*
 * @brief Used for compare of policies parameters mapped with specific
 * application ids
 */
bool operator!=(const policy_table::ApplicationParams& first,
                const policy_table::ApplicationParams& second);

/*
 * @brief Helper struct for checking changes of application policies, which
 * come with update along with current data snapshot
 * In case of policies changed for some application, current data will be
 * updated and notification will be sent to application
 */
struct CheckAppPolicy {
  CheckAppPolicy(PolicyManagerImpl* pm,
                 const utils::SharedPtr<policy_table::Table> update);
  bool HasSameGroups(const AppPoliciesValueType& app_policy,
                     AppPermissions* perms) const;
  bool IsNewAppication(const std::string& application_id) const;
  void SendNotification(const AppPoliciesValueType& app_policy) const;
  void SendOnPendingPermissions(const AppPoliciesValueType& app_policy,
                                AppPermissions permissions) const;
  bool IsAppRevoked(const AppPoliciesValueType& app_policy) const;
  bool operator()(const AppPoliciesValueType& app_policy);
 private:
  PolicyManagerImpl* pm_;
  const utils::SharedPtr<policy_table::Table> update_;
};

/*
 * @brief Fill notification data with merged rpc permissions for hmi levels and
 * parameters
 */
struct FillNotificationData {
  FillNotificationData(Permissions& data, PermissionState group_state);
  bool operator()(const RpcValueType& rpc);
  void UpdateHMILevels(const policy_table::HmiLevels& in_hmi,
                       std::set<HMILevel>& out_hmi);
  void UpdateParameters(const policy_table::Parameters& in_parameters,
                        std::set<Parameter>& out_parameter);
 private:
  void ExcludeDisAllowed();
  std::string current_key_;
  std::string allowed_key_;
  std::string disallowed_key_;
  Permissions& data_;
};

/*
 * @brief Check for functional group presence and pass it to helper struct,
 * which gather data for notification sending
 */
struct ProcessFunctionalGroup {
  ProcessFunctionalGroup(
      const policy_table::FunctionalGroupings& fg,
      const std::vector<FunctionalGroupPermission>& group_permissions,
      Permissions& data);
  bool operator()(const StringsValueType& group_name);
 private:
  PermissionState GetGroupState(const std::string& group_name);
  const policy_table::FunctionalGroupings& fg_;
  Permissions& data_;
  const std::vector<FunctionalGroupPermission>& group_permissions_;
};

struct FunctionalGroupInserter {
  FunctionalGroupInserter(const policy_table::Strings& preconsented_groups,
                          PermissionsList& list);
  void operator()(const StringsValueType& group_name);
 private:
  PermissionsList& list_;
  const policy_table::Strings& preconsented_;
};

}

#endif // SRC_COMPONENTS_POLICY_INCLUDE_POLICY_POLICY_HELPER_H_