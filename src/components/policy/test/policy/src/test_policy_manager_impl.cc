/* Copyright (c) 2013, Ford Motor Company
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided with the
 * distribution.
 *
 * Neither the name of the Ford Motor Company nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <vector>
#include "mock_pt_representation.h"
#include "mock_pt_ext_representation.h"
#include "policy/policy_table.h"
#include "policy/policy_manager_impl.h"
#include "json/writer.h"

using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::SetArgPointee;

using ::policy::PTRepresentation;
using ::policy::MockPTRepresentation;
using ::policy::MockPTExtRepresentation;
using ::policy::PolicyManagerImpl;
using ::policy::PolicyTable;
using ::policy::EndpointUrls;

namespace test {
namespace components {
namespace policy {

class PolicyManagerImplTest : public ::testing::Test {
  protected:
    static void SetUpTestCase() {
    }

    static void TearDownTestCase() {
    }
};

TEST_F(PolicyManagerImplTest, ExceededIgnitionCycles) {
  ::testing::NiceMock<MockPTRepresentation> mock_pt;

  EXPECT_CALL(mock_pt, IgnitionCyclesBeforeExchange()).Times(2).WillOnce(
    Return(5)).WillOnce(Return(0));
  EXPECT_CALL(
    mock_pt, IncrementIgnitionCycles()).Times(1);

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  EXPECT_FALSE(manager->ExceededIgnitionCycles());
  manager->IncrementIgnitionCycles();
  EXPECT_TRUE(manager->ExceededIgnitionCycles());
}

TEST_F(PolicyManagerImplTest, ExceededDays) {
  ::testing::NiceMock<MockPTRepresentation> mock_pt;

  EXPECT_CALL(mock_pt, DaysBeforeExchange(_)).Times(2).WillOnce(Return(5))
  .WillOnce(Return(0));

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  EXPECT_FALSE(manager->ExceededDays(5));
  EXPECT_TRUE(manager->ExceededDays(15));
}

TEST_F(PolicyManagerImplTest, ExceededKilometers) {
  ::testing::NiceMock<MockPTRepresentation> mock_pt;

  EXPECT_CALL(mock_pt, KilometersBeforeExchange(_)).Times(2).WillOnce(
    Return(50)).WillOnce(Return(0));

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  EXPECT_FALSE(manager->ExceededKilometers(50));
  EXPECT_TRUE(manager->ExceededKilometers(150));
}

TEST_F(PolicyManagerImplTest, NextRetryTimeout) {
  ::testing::NiceMock<MockPTRepresentation> mock_pt;
  std::vector<int> seconds;
  seconds.push_back(50);
  seconds.push_back(100);
  seconds.push_back(200);

  EXPECT_CALL(mock_pt, TimeoutResponse()).Times(1).WillOnce(Return(60));
  EXPECT_CALL(mock_pt,
              SecondsBetweenRetries(_)).Times(1).WillOnce(
                DoAll(SetArgPointee<0>(seconds), Return(true)));

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  EXPECT_EQ(50, manager->NextRetryTimeout());
  EXPECT_EQ(100, manager->NextRetryTimeout());
  EXPECT_EQ(200, manager->NextRetryTimeout());
  EXPECT_EQ(0, manager->NextRetryTimeout());
}

TEST_F(PolicyManagerImplTest, GetUpdateUrl) {
  ::testing::NiceMock<MockPTRepresentation> mock_pt;
  EndpointUrls urls_1234, urls_4321;
  urls_1234.push_back(::policy::EndpointData("http://ford.com/cloud/1"));
  urls_1234.push_back(::policy::EndpointData("http://ford.com/cloud/2"));
  urls_1234.push_back(::policy::EndpointData("http://ford.com/cloud/3"));
  urls_4321.push_back(::policy::EndpointData("http://panasonic.com/cloud/1"));
  urls_4321.push_back(::policy::EndpointData("http://panasonic.com/cloud/2"));
  urls_4321.push_back(::policy::EndpointData("http://panasonic.com/cloud/3"));

  EXPECT_CALL(mock_pt, GetUpdateUrls(7)).Times(4).WillRepeatedly(
    Return(urls_1234));
  EXPECT_CALL(mock_pt,
              GetUpdateUrls(4)).Times(2).WillRepeatedly(Return(urls_4321));

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  EXPECT_EQ("http://ford.com/cloud/1", manager->GetUpdateUrl(7));
  EXPECT_EQ("http://ford.com/cloud/2", manager->GetUpdateUrl(7));
  EXPECT_EQ("http://ford.com/cloud/3", manager->GetUpdateUrl(7));
  EXPECT_EQ("http://panasonic.com/cloud/1", manager->GetUpdateUrl(4));
  EXPECT_EQ("http://ford.com/cloud/2", manager->GetUpdateUrl(7));
  EXPECT_EQ("http://panasonic.com/cloud/3", manager->GetUpdateUrl(7));
}

TEST_F(PolicyManagerImplTest, RefreshRetrySequence) {
  ::testing::NiceMock<MockPTRepresentation> mock_pt;
  std::vector<int> seconds, seconds_empty;
  seconds.push_back(50);
  seconds.push_back(100);
  seconds.push_back(200);

  EXPECT_CALL(mock_pt, TimeoutResponse()).Times(2).WillOnce(Return(0)).WillOnce(
    Return(60));
  EXPECT_CALL(mock_pt, SecondsBetweenRetries(_)).Times(2).WillOnce(
    DoAll(SetArgPointee<0>(seconds_empty), Return(true))).WillOnce(
      DoAll(SetArgPointee<0>(seconds), Return(true)));

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  manager->RefreshRetrySequence();
  EXPECT_EQ(60, manager->TimeoutExchange());
  EXPECT_EQ(50, manager->NextRetryTimeout());
  EXPECT_EQ(100, manager->NextRetryTimeout());
  EXPECT_EQ(200, manager->NextRetryTimeout());
}

TEST_F(PolicyManagerImplTest, IncrementGlobalCounter) {
  ::testing::NiceMock<MockPTExtRepresentation> mock_pt;

  EXPECT_CALL(mock_pt, Increment("count_of_sync_reboots")).Times(1);

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  manager->Increment(usage_statistics::SYNC_REBOOTS);
}

TEST_F(PolicyManagerImplTest, IncrementAppCounter) {
  ::testing::NiceMock<MockPTExtRepresentation> mock_pt;

  EXPECT_CALL(mock_pt, Increment("12345", "count_of_user_selections")).Times(1);

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  manager->Increment("12345", usage_statistics::USER_SELECTIONS);
}

TEST_F(PolicyManagerImplTest, SetAppInfo) {
  ::testing::NiceMock<MockPTExtRepresentation> mock_pt;

  EXPECT_CALL(mock_pt, Set("12345", "app_registration_language_gui", "de-de")).
  Times(1);

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  manager->Set("12345", usage_statistics::LANGUAGE_GUI, "de-de");
}

TEST_F(PolicyManagerImplTest, AddAppStopwatch) {
  ::testing::NiceMock<MockPTExtRepresentation> mock_pt;

  EXPECT_CALL(mock_pt, Add("12345", "minutes_hmi_full", 30)).Times(1);

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  manager->Add("12345", usage_statistics::SECONDS_HMI_FULL, 30);
}

TEST_F(PolicyManagerImplTest, LoadPTFromFile) {
  ::testing::NiceMock<MockPTRepresentation> mock_pt;

  EXPECT_CALL(mock_pt, Init()).WillOnce(Return(::policy::NONE))
  //.WillOnce(Return(::policy::EXISTS));
  //.WillOnce(Return(::policy::SUCCESS))
  .WillOnce(Return(::policy::FAIL));

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  EXPECT_FALSE(manager->LoadPTFromFile("filename"));
  // TODO(AOleynik): Sometimes fails, check this
  //  EXPECT_TRUE(manager->LoadPTFromFile("filename"));
  //  EXPECT_TRUE(manager->LoadPTFromFile("filename"));
  EXPECT_FALSE(manager->LoadPTFromFile("filename"));
}

TEST_F(PolicyManagerImplTest, CheckPermissions) {
  ::testing::NiceMock<MockPTRepresentation> mock_pt;

  ::policy::CheckPermissionResult result;
  result.hmi_level_permitted = true;
  result.list_of_allowed_params = new std::vector< ::policy::PTString>();
  result.list_of_allowed_params->push_back("FULL");
  result.list_of_allowed_params->push_back("NONE");
  result.list_of_allowed_params->push_back("LIMITED");
  result.list_of_allowed_params->push_back("BACKGROUND");

  ::policy::PTString app_id = "12345678";
  ::policy::PTString hmi_level = "FULL";
  ::policy::PTString rpc = "Alert";

  EXPECT_CALL(mock_pt, CheckPermissions(app_id, hmi_level, rpc)).WillOnce(
    Return(result));

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  ::policy::CheckPermissionResult out_result = manager->CheckPermissions(
        app_id, hmi_level, rpc);
  EXPECT_TRUE(out_result.hmi_level_permitted == true);
}

TEST_F(PolicyManagerImplTest, LoadPT) {
  ::testing::NiceMock<MockPTRepresentation> mock_pt;

  ::policy_table::Table table;
  Json::Value value = table.ToJsonValue();
  Json::FastWriter writer;
  std::string json = writer.write(value);
  ::policy::BinaryMessage msg(json.begin(), json.end());

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));

  // TODO(AOleynik): Validation is not enabled yet, so test fails
  // TODO(AOleynik): Segfault occurs, check
  //EXPECT_FALSE(manager->LoadPT(msg));
}

TEST_F(PolicyManagerImplTest, RequestPTUpdate) {
  ::testing::NiceMock<MockPTRepresentation> mock_pt;

  ::utils::SharedPtr< ::policy_table::Table> p_table =
    new ::policy_table::Table();
  Json::Value value = p_table.get()->ToJsonValue();
  Json::FastWriter writer;
  std::string json(writer.write(value));
  ::policy::BinaryMessageSptr p_msg = new ::policy::BinaryMessage(json.begin(),
      json.end());

  EXPECT_CALL(mock_pt, GenerateSnapshot()).WillOnce(Return(p_table));

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  ::policy::BinaryMessageSptr p_result = manager->RequestPTUpdate();
  EXPECT_TRUE(*p_msg.get() == *p_result.get());
}

TEST_F(PolicyManagerImplTest, ResetUserConsent) {
  ::testing::NiceMock<MockPTExtRepresentation> mock_pt;

  EXPECT_CALL(mock_pt, ResetUserConsent()).WillOnce(Return(true)).WillOnce(
    Return(false));

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  EXPECT_TRUE(manager->ResetUserConsent());
  EXPECT_FALSE(manager->ResetUserConsent());
}

TEST_F(PolicyManagerImplTest, CheckAppPolicyState) {
  ::testing::NiceMock<MockPTExtRepresentation> mock_pt;

  // TODO(AOleynik): Implementation of method should be changed to avoid
  // using of snapshot
  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  //manager->CheckAppPolicyState("12345678");
}

TEST_F(PolicyManagerImplTest, GetPolicyTableStatus) {
  ::testing::NiceMock<MockPTExtRepresentation> mock_pt;

  PolicyManagerImpl* manager = new PolicyManagerImpl();
  manager->ResetDefaultPT(::policy::PolicyTable(&mock_pt));
  // TODO(AOleynik): Test is not finished, to be continued
  //manager->GetPolicyTableStatus();
}

}  // namespace policy
}  // namespace components
}  // namespace test

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
