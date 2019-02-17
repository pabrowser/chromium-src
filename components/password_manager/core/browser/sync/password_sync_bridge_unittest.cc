// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/sync/password_sync_bridge.h"

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind_test_util.h"
#include "components/password_manager/core/browser/password_store_sync.h"
#include "components/sync/model/data_batch.h"
#include "components/sync/model/entity_change.h"
#include "components/sync/model/metadata_batch.h"
#include "components/sync/model/mock_model_type_change_processor.h"
#include "components/sync/model_impl/in_memory_metadata_change_list.h"
#include "components/sync/model_impl/sync_metadata_store_change_list.h"
#include "components/sync/test/test_matchers.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace password_manager {

namespace {

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::NotNull;
using testing::Return;
using testing::UnorderedElementsAre;

constexpr char kSignonRealm1[] = "abc";
constexpr char kSignonRealm2[] = "def";
constexpr char kSignonRealm3[] = "xyz";

// |*arg| must be of type EntityData.
MATCHER_P(EntityDataHasSignonRealm, expected_signon_realm, "") {
  return arg->specifics.password()
             .client_only_encrypted_data()
             .signon_realm() == expected_signon_realm;
}

// |*arg| must be of type PasswordForm.
MATCHER_P(FormHasSignonRealm, expected_signon_realm, "") {
  return arg.signon_realm == expected_signon_realm;
}

// |*arg| must be of type PasswordStoreChange.
MATCHER_P(ChangeHasPrimaryKey, expected_primary_key, "") {
  return arg.primary_key() == expected_primary_key;
}

// |*arg| must be of type SyncMetadataStoreChangeList.
MATCHER_P(IsSyncMetadataStoreChangeListWithStore, expected_metadata_store, "") {
  return static_cast<const syncer::SyncMetadataStoreChangeList*>(arg)
             ->GetMetadataStoreForTesting() == expected_metadata_store;
}

sync_pb::PasswordSpecifics CreateSpecifics(const std::string& origin,
                                           const std::string& username_element,
                                           const std::string& username_value,
                                           const std::string& password_element,
                                           const std::string& signon_realm) {
  sync_pb::EntitySpecifics password_specifics;
  sync_pb::PasswordSpecificsData* password_data =
      password_specifics.mutable_password()
          ->mutable_client_only_encrypted_data();
  password_data->set_origin(origin);
  password_data->set_username_element(username_element);
  password_data->set_username_value(username_value);
  password_data->set_password_element(password_element);
  password_data->set_signon_realm(signon_realm);
  return password_specifics.password();
}

sync_pb::PasswordSpecifics CreateSpecificsWithSignonRealm(
    const std::string& signon_realm) {
  return CreateSpecifics("http://www.origin.com", "username_element",
                         "username_value", "password_element", signon_realm);
}

autofill::PasswordForm MakePasswordForm(const std::string& signon_realm) {
  autofill::PasswordForm form;
  form.origin = GURL("http://www.origin.com");
  form.username_element = base::UTF8ToUTF16("username_element");
  form.username_value = base::UTF8ToUTF16("username_value");
  form.password_element = base::UTF8ToUTF16("password_element");
  form.signon_realm = signon_realm;
  return form;
}

// Creates an EntityData/EntityDataPtr around a copy of the given specifics.
syncer::EntityDataPtr SpecificsToEntity(
    const sync_pb::PasswordSpecifics& specifics) {
  syncer::EntityData data;
  // These tests do not care about the tag hash, but EntityData and friends
  // cannot differentiate between the default EntityData object if the hash
  // is unset, which causes pass/copy operations to no-op and things start to
  // break, so we throw in a junk value and forget about it.
  data.client_tag_hash = "junk";
  *data.specifics.mutable_password() = specifics;
  return data.PassToPtr();
}

// A mini database class the supports Add/Update/Remove functionality. It also
// supports an auto increment primary key that starts from 1. It will be used to
// empower the MockPasswordStoreSync be forwarding all database calls to an
// instance of this class.
class FakeDatabase {
 public:
  FakeDatabase() = default;
  ~FakeDatabase() = default;

  bool ReadAllLogins(PrimaryKeyToFormMap* map) {
    map->clear();
    for (const auto& pair : data_) {
      map->emplace(pair.first,
                   std::make_unique<autofill::PasswordForm>(*pair.second));
    }
    return true;
  }

  PasswordStoreChangeList AddLogin(const autofill::PasswordForm& form) {
    data_[primary_key_] = std::make_unique<autofill::PasswordForm>(form);
    return {
        PasswordStoreChange(PasswordStoreChange::ADD, form, primary_key_++)};
  }

  PasswordStoreChangeList AddLoginForPrimaryKey(
      int primary_key,
      const autofill::PasswordForm& form) {
    DCHECK_EQ(0U, data_.count(primary_key));
    data_[primary_key] = std::make_unique<autofill::PasswordForm>(form);
    return {PasswordStoreChange(PasswordStoreChange::ADD, form, primary_key)};
  }

  PasswordStoreChangeList UpdateLogin(const autofill::PasswordForm& form) {
    int key = GetPrimaryKey(form);
    DCHECK_NE(-1, key);
    data_[key] = std::make_unique<autofill::PasswordForm>(form);
    return {PasswordStoreChange(PasswordStoreChange::UPDATE, form, key)};
  }

  PasswordStoreChangeList RemoveLogin(int key) {
    DCHECK_NE(0U, data_.count(key));
    autofill::PasswordForm form = *data_[key];
    data_.erase(key);
    return {PasswordStoreChange(PasswordStoreChange::REMOVE, form, key)};
  }

 private:
  int GetPrimaryKey(const autofill::PasswordForm& form) const {
    for (const auto& pair : data_) {
      if (ArePasswordFormUniqueKeyEqual(*pair.second, form)) {
        return pair.first;
      }
    }
    return -1;
  }

  int primary_key_ = 1;
  PrimaryKeyToFormMap data_;

  DISALLOW_COPY_AND_ASSIGN(FakeDatabase);
};

class MockSyncMetadataStore : public PasswordStoreSync::MetadataStore {
 public:
  MockSyncMetadataStore() = default;
  ~MockSyncMetadataStore() = default;

  MOCK_METHOD0(GetAllSyncMetadata, std::unique_ptr<syncer::MetadataBatch>());
  MOCK_METHOD3(UpdateSyncMetadata,
               bool(syncer::ModelType,
                    const std::string&,
                    const sync_pb::EntityMetadata&));
  MOCK_METHOD2(ClearSyncMetadata, bool(syncer::ModelType, const std::string&));
  MOCK_METHOD2(UpdateModelTypeState,
               bool(syncer::ModelType, const sync_pb::ModelTypeState&));
  MOCK_METHOD1(ClearModelTypeState, bool(syncer::ModelType));
};

class MockPasswordStoreSync : public PasswordStoreSync {
 public:
  MockPasswordStoreSync() = default;
  ~MockPasswordStoreSync() = default;

  MOCK_METHOD1(FillAutofillableLogins,
               bool(std::vector<std::unique_ptr<autofill::PasswordForm>>*));
  MOCK_METHOD1(FillBlacklistLogins,
               bool(std::vector<std::unique_ptr<autofill::PasswordForm>>*));
  MOCK_METHOD1(ReadAllLogins, bool(PrimaryKeyToFormMap*));
  MOCK_METHOD1(RemoveLoginByPrimaryKeySync, PasswordStoreChangeList(int));
  MOCK_METHOD0(DeleteUndecryptableLogins, DatabaseCleanupResult());
  MOCK_METHOD1(AddLoginSync,
               PasswordStoreChangeList(const autofill::PasswordForm&));
  MOCK_METHOD1(UpdateLoginSync,
               PasswordStoreChangeList(const autofill::PasswordForm&));
  MOCK_METHOD1(RemoveLoginSync,
               PasswordStoreChangeList(const autofill::PasswordForm&));
  MOCK_METHOD1(NotifyLoginsChanged, void(const PasswordStoreChangeList&));
  MOCK_METHOD0(BeginTransaction, bool());
  MOCK_METHOD0(CommitTransaction, bool());
  MOCK_METHOD0(GetMetadataStore, PasswordStoreSync::MetadataStore*());
};

}  // namespace

class PasswordSyncBridgeTest : public testing::Test {
 public:
  PasswordSyncBridgeTest() {
    ON_CALL(mock_password_store_sync_, GetMetadataStore())
        .WillByDefault(testing::Return(&mock_sync_metadata_store_sync_));
    ON_CALL(mock_password_store_sync_, ReadAllLogins(_))
        .WillByDefault(Invoke(&fake_db_, &FakeDatabase::ReadAllLogins));
    ON_CALL(mock_password_store_sync_, AddLoginSync(_))
        .WillByDefault(Invoke(&fake_db_, &FakeDatabase::AddLogin));
    ON_CALL(mock_password_store_sync_, UpdateLoginSync(_))
        .WillByDefault(Invoke(&fake_db_, &FakeDatabase::UpdateLogin));
    ON_CALL(mock_password_store_sync_, RemoveLoginByPrimaryKeySync(_))
        .WillByDefault(Invoke(&fake_db_, &FakeDatabase::RemoveLogin));

    bridge_ = std::make_unique<PasswordSyncBridge>(
        mock_processor_.CreateForwardingProcessor(),
        &mock_password_store_sync_);

    // It's the responsibility of the PasswordStoreSync to inform the bridge
    // about changes in the password store. The bridge notifies the
    // PasswordStoreSync about the new changes even if they are initiated by the
    // bridge itself.
    ON_CALL(mock_password_store_sync_, NotifyLoginsChanged(_))
        .WillByDefault(
            Invoke(bridge(), &PasswordSyncBridge::ActOnPasswordStoreChanges));

    ON_CALL(mock_sync_metadata_store_sync_, GetAllSyncMetadata())
        .WillByDefault(
            []() { return std::make_unique<syncer::MetadataBatch>(); });
    ON_CALL(mock_sync_metadata_store_sync_, UpdateSyncMetadata(_, _, _))
        .WillByDefault(testing::Return(true));
    ON_CALL(mock_sync_metadata_store_sync_, ClearSyncMetadata(_, _))
        .WillByDefault(testing::Return(true));
    ON_CALL(mock_sync_metadata_store_sync_, UpdateModelTypeState(_, _))
        .WillByDefault(testing::Return(true));
    ON_CALL(mock_sync_metadata_store_sync_, ClearModelTypeState(_))
        .WillByDefault(testing::Return(true));
  }

  ~PasswordSyncBridgeTest() override {}

  base::Optional<sync_pb::PasswordSpecifics> GetDataFromBridge(
      const std::string& storage_key) {
    std::unique_ptr<syncer::DataBatch> batch;
    bridge_->GetData({storage_key},
                     base::BindLambdaForTesting(
                         [&](std::unique_ptr<syncer::DataBatch> in_batch) {
                           batch = std::move(in_batch);
                         }));
    EXPECT_THAT(batch, NotNull());
    if (!batch || !batch->HasNext()) {
      return base::nullopt;
    }
    const syncer::KeyAndData& data_pair = batch->Next();
    EXPECT_THAT(data_pair.first, Eq(storage_key));
    EXPECT_FALSE(batch->HasNext());
    return data_pair.second->specifics.password();
  }

  FakeDatabase* fake_db() { return &fake_db_; }

  PasswordSyncBridge* bridge() { return bridge_.get(); }

  syncer::MockModelTypeChangeProcessor& mock_processor() {
    return mock_processor_;
  }

  MockSyncMetadataStore* mock_sync_metadata_store_sync() {
    return &mock_sync_metadata_store_sync_;
  }

  MockPasswordStoreSync* mock_password_store_sync() {
    return &mock_password_store_sync_;
  }

 private:
  FakeDatabase fake_db_;
  testing::NiceMock<syncer::MockModelTypeChangeProcessor> mock_processor_;
  testing::NiceMock<MockSyncMetadataStore> mock_sync_metadata_store_sync_;
  testing::NiceMock<MockPasswordStoreSync> mock_password_store_sync_;
  std::unique_ptr<PasswordSyncBridge> bridge_;
};

TEST_F(PasswordSyncBridgeTest, ShouldComputeClientTagHash) {
  syncer::EntityData data;
  *data.specifics.mutable_password() =
      CreateSpecifics("http://www.origin.com", "username_element",
                      "username_value", "password_element", "signon_realm");

  EXPECT_THAT(
      bridge()->GetClientTag(data),
      Eq("http%3A//www.origin.com/"
         "|username_element|username_value|password_element|signon_realm"));
}

TEST_F(PasswordSyncBridgeTest, ShouldForwardLocalChangesToTheProcessor) {
  ON_CALL(mock_processor(), IsTrackingMetadata()).WillByDefault(Return(true));

  PasswordStoreChangeList changes;
  changes.push_back(PasswordStoreChange(
      PasswordStoreChange::ADD, MakePasswordForm(kSignonRealm1), /*id=*/1));
  changes.push_back(PasswordStoreChange(
      PasswordStoreChange::UPDATE, MakePasswordForm(kSignonRealm2), /*id=*/2));
  changes.push_back(PasswordStoreChange(
      PasswordStoreChange::REMOVE, MakePasswordForm(kSignonRealm3), /*id=*/3));
  PasswordStoreSync::MetadataStore* store =
      mock_password_store_sync()->GetMetadataStore();
  EXPECT_CALL(mock_processor(),
              Put("1", EntityDataHasSignonRealm(kSignonRealm1),
                  IsSyncMetadataStoreChangeListWithStore(store)));
  EXPECT_CALL(mock_processor(),
              Put("2", EntityDataHasSignonRealm(kSignonRealm2),
                  IsSyncMetadataStoreChangeListWithStore(store)));
  EXPECT_CALL(mock_processor(),
              Delete("3", IsSyncMetadataStoreChangeListWithStore(store)));

  bridge()->ActOnPasswordStoreChanges(changes);
}

TEST_F(PasswordSyncBridgeTest,
       ShouldNotForwardLocalChangesToTheProcessorIfSyncDisabled) {
  ON_CALL(mock_processor(), IsTrackingMetadata()).WillByDefault(Return(false));

  PasswordStoreChangeList changes;
  changes.push_back(PasswordStoreChange(
      PasswordStoreChange::ADD, MakePasswordForm(kSignonRealm1), /*id=*/1));
  changes.push_back(PasswordStoreChange(
      PasswordStoreChange::UPDATE, MakePasswordForm(kSignonRealm2), /*id=*/2));
  changes.push_back(PasswordStoreChange(
      PasswordStoreChange::REMOVE, MakePasswordForm(kSignonRealm3), /*id=*/3));

  EXPECT_CALL(mock_processor(), Put(_, _, _)).Times(0);
  EXPECT_CALL(mock_processor(), Delete(_, _)).Times(0);

  bridge()->ActOnPasswordStoreChanges(changes);
}

TEST_F(PasswordSyncBridgeTest, ShouldApplyEmptySyncChangesWithoutError) {
  base::Optional<syncer::ModelError> error = bridge()->ApplySyncChanges(
      bridge()->CreateMetadataChangeList(), syncer::EntityChangeList());
  EXPECT_FALSE(error);
}

TEST_F(PasswordSyncBridgeTest, ShouldApplyMetadataWithEmptySyncChanges) {
  const std::string kStorageKey = "1";
  const std::string kServerId = "TestServerId";
  sync_pb::EntityMetadata metadata;
  metadata.set_server_id(kServerId);
  auto metadata_change_list =
      std::make_unique<syncer::InMemoryMetadataChangeList>();
  metadata_change_list->UpdateMetadata(kStorageKey, metadata);

  EXPECT_CALL(*mock_password_store_sync(), NotifyLoginsChanged(_)).Times(0);

  EXPECT_CALL(*mock_sync_metadata_store_sync(),
              UpdateSyncMetadata(syncer::PASSWORDS, kStorageKey, _));

  base::Optional<syncer::ModelError> error = bridge()->ApplySyncChanges(
      std::move(metadata_change_list), syncer::EntityChangeList());
  EXPECT_FALSE(error);
}

TEST_F(PasswordSyncBridgeTest, ShouldApplyRemoteCreation) {
  ON_CALL(mock_processor(), IsTrackingMetadata()).WillByDefault(Return(true));
  // Since this remote creation is the first entry in the FakeDatabase, it will
  // be assigned a primary key 1.
  const std::string kStorageKey = "1";

  sync_pb::PasswordSpecifics specifics =
      CreateSpecificsWithSignonRealm(kSignonRealm1);

  testing::InSequence in_sequence;
  EXPECT_CALL(*mock_password_store_sync(), BeginTransaction());
  EXPECT_CALL(*mock_password_store_sync(),
              AddLoginSync(FormHasSignonRealm(kSignonRealm1)));
  EXPECT_CALL(mock_processor(), UpdateStorageKey(_, kStorageKey, _));
  EXPECT_CALL(*mock_password_store_sync(), CommitTransaction());
  EXPECT_CALL(
      *mock_password_store_sync(),
      NotifyLoginsChanged(UnorderedElementsAre(ChangeHasPrimaryKey(1))));

  // Processor shouldn't be notified about remote changes.
  EXPECT_CALL(mock_processor(), Put(_, _, _)).Times(0);

  base::Optional<syncer::ModelError> error = bridge()->ApplySyncChanges(
      bridge()->CreateMetadataChangeList(),
      {syncer::EntityChange::CreateAdd(
          /*storage_key=*/"", SpecificsToEntity(specifics))});
  EXPECT_FALSE(error);
}

TEST_F(PasswordSyncBridgeTest, ShouldApplyRemoteUpdate) {
  const int kPrimaryKey = 1000;
  const std::string kStorageKey = "1000";
  // Add the form to the DB.
  fake_db()->AddLoginForPrimaryKey(kPrimaryKey,
                                   MakePasswordForm(kSignonRealm1));

  sync_pb::PasswordSpecifics specifics =
      CreateSpecificsWithSignonRealm(kSignonRealm1);

  testing::InSequence in_sequence;
  EXPECT_CALL(*mock_password_store_sync(), BeginTransaction());
  EXPECT_CALL(*mock_password_store_sync(),
              UpdateLoginSync(FormHasSignonRealm(kSignonRealm1)));
  EXPECT_CALL(*mock_password_store_sync(), CommitTransaction());
  EXPECT_CALL(*mock_password_store_sync(),
              NotifyLoginsChanged(
                  UnorderedElementsAre(ChangeHasPrimaryKey(kPrimaryKey))));

  // Processor shouldn't be notified about remote changes.
  EXPECT_CALL(mock_processor(), Put(_, _, _)).Times(0);
  EXPECT_CALL(mock_processor(), UpdateStorageKey(_, _, _)).Times(0);

  base::Optional<syncer::ModelError> error = bridge()->ApplySyncChanges(
      bridge()->CreateMetadataChangeList(),
      {syncer::EntityChange::CreateUpdate(kStorageKey,
                                          SpecificsToEntity(specifics))});
  EXPECT_FALSE(error);
}

TEST_F(PasswordSyncBridgeTest, ShouldApplyRemoteDeletion) {
  const int kPrimaryKey = 1000;
  const std::string kStorageKey = "1000";
  // Add the form to the DB.
  fake_db()->AddLoginForPrimaryKey(kPrimaryKey,
                                   MakePasswordForm(kSignonRealm1));

  testing::InSequence in_sequence;
  EXPECT_CALL(*mock_password_store_sync(), BeginTransaction());
  EXPECT_CALL(*mock_password_store_sync(),
              RemoveLoginByPrimaryKeySync(kPrimaryKey));
  EXPECT_CALL(*mock_password_store_sync(), CommitTransaction());
  EXPECT_CALL(*mock_password_store_sync(),
              NotifyLoginsChanged(
                  UnorderedElementsAre(ChangeHasPrimaryKey(kPrimaryKey))));

  // Processor shouldn't be notified about remote changes.
  EXPECT_CALL(mock_processor(), Delete(_, _)).Times(0);

  base::Optional<syncer::ModelError> error = bridge()->ApplySyncChanges(
      bridge()->CreateMetadataChangeList(),
      {syncer::EntityChange::CreateDelete(kStorageKey)});
  EXPECT_FALSE(error);
}

TEST_F(PasswordSyncBridgeTest, ShouldGetDataForStorageKey) {
  const int kPrimaryKey1 = 1000;
  const int kPrimaryKey2 = 1001;
  const std::string kPrimaryKeyStr1 = "1000";
  const std::string kPrimaryKeyStr2 = "1001";
  autofill::PasswordForm form1 = MakePasswordForm(kSignonRealm1);
  autofill::PasswordForm form2 = MakePasswordForm(kSignonRealm2);

  fake_db()->AddLoginForPrimaryKey(kPrimaryKey1, form1);
  fake_db()->AddLoginForPrimaryKey(kPrimaryKey2, form2);

  base::Optional<sync_pb::PasswordSpecifics> optional_specifics =
      GetDataFromBridge(/*storage_key=*/kPrimaryKeyStr1);
  ASSERT_TRUE(optional_specifics.has_value());
  EXPECT_EQ(
      kSignonRealm1,
      optional_specifics.value().client_only_encrypted_data().signon_realm());

  optional_specifics = GetDataFromBridge(/*storage_key=*/kPrimaryKeyStr2);
  ASSERT_TRUE(optional_specifics.has_value());
  EXPECT_EQ(kSignonRealm2,
            optional_specifics->client_only_encrypted_data().signon_realm());
}

TEST_F(PasswordSyncBridgeTest, ShouldNotGetDataForNonExistingStorageKey) {
  const std::string kPrimaryKeyStr = "1";

  base::Optional<sync_pb::PasswordSpecifics> optional_specifics =
      GetDataFromBridge(/*storage_key=*/kPrimaryKeyStr);
  EXPECT_FALSE(optional_specifics.has_value());
}

TEST_F(PasswordSyncBridgeTest, ShouldMergeSyncRemoteAndLocalPasswords) {
  ON_CALL(mock_processor(), IsTrackingMetadata()).WillByDefault(Return(true));
  // Setup the test to have Form 1 and Form 2 stored locally, and Form 2 and
  // Form 3 coming as remote changes. We will assign primary keys for Form 1 and
  // Form 2. Form 3 will arrive as remote creation, and FakeDatabase will assign
  // it primary key 1.
  const int kPrimaryKey1 = 1000;
  const int kPrimaryKey2 = 1001;
  const int kExpectedPrimaryKey3 = 1;
  const std::string kPrimaryKeyStr1 = "1000";
  const std::string kPrimaryKeyStr2 = "1001";
  const std::string kExpectedPrimaryKeyStr3 = "1";
  autofill::PasswordForm form1 = MakePasswordForm(kSignonRealm1);
  autofill::PasswordForm form2 = MakePasswordForm(kSignonRealm2);
  autofill::PasswordForm form3 = MakePasswordForm(kSignonRealm3);
  sync_pb::PasswordSpecifics specifics1 =
      CreateSpecificsWithSignonRealm(kSignonRealm1);
  sync_pb::PasswordSpecifics specifics2 =
      CreateSpecificsWithSignonRealm(kSignonRealm2);
  sync_pb::PasswordSpecifics specifics3 =
      CreateSpecificsWithSignonRealm(kSignonRealm3);

  fake_db()->AddLoginForPrimaryKey(kPrimaryKey1, form1);
  fake_db()->AddLoginForPrimaryKey(kPrimaryKey2, form2);

  // Form 1 will be added to the change processor, Form 2 will be updated in the
  // password sync store, and Form 3 will be added to the password store sync.

  // Interactions should happen in this order:
  //           +--> Put(1) ------------------------------------+
  //           |                                               |
  // Begin() --|--> UpdateLoginSync(2) --> UpdateStorageKey(2)-|--> Commit()
  //           |                                               |
  //           +--> AddLoginSync (3)   --> UpdateStorageKey(3)-+

  testing::Sequence s1, s2, s3;
  EXPECT_CALL(*mock_password_store_sync(), BeginTransaction())
      .InSequence(s1, s2, s3);
  EXPECT_CALL(mock_processor(),
              Put(kPrimaryKeyStr1, EntityDataHasSignonRealm(kSignonRealm1), _))
      .InSequence(s1);
  EXPECT_CALL(*mock_password_store_sync(),
              UpdateLoginSync(FormHasSignonRealm(kSignonRealm2)))
      .InSequence(s2);
  EXPECT_CALL(*mock_password_store_sync(),
              AddLoginSync(FormHasSignonRealm(kSignonRealm3)))
      .InSequence(s3);

  EXPECT_CALL(mock_processor(), UpdateStorageKey(_, kPrimaryKeyStr2, _))
      .InSequence(s2);
  EXPECT_CALL(mock_processor(), UpdateStorageKey(_, kExpectedPrimaryKeyStr3, _))
      .InSequence(s3);
  EXPECT_CALL(*mock_password_store_sync(), CommitTransaction())
      .InSequence(s1, s2, s3);

  EXPECT_CALL(*mock_password_store_sync(),
              NotifyLoginsChanged(UnorderedElementsAre(
                  ChangeHasPrimaryKey(kPrimaryKey2),
                  ChangeHasPrimaryKey(kExpectedPrimaryKey3))))
      .InSequence(s1, s2, s3);

  // Processor shouldn't be informed about Form 2 or Form 3.
  EXPECT_CALL(mock_processor(), Put(kPrimaryKeyStr2, _, _)).Times(0);
  EXPECT_CALL(mock_processor(), Put(kExpectedPrimaryKeyStr3, _, _)).Times(0);

  base::Optional<syncer::ModelError> error = bridge()->MergeSyncData(
      bridge()->CreateMetadataChangeList(),
      {syncer::EntityChange::CreateAdd(
           /*storage_key=*/"", SpecificsToEntity(specifics2)),
       syncer::EntityChange::CreateAdd(
           /*storage_key=*/"", SpecificsToEntity(specifics3))});
  EXPECT_FALSE(error);
}

TEST_F(PasswordSyncBridgeTest, ShouldGetAllDataForDebuggingWithHiddenPassword) {
  const int kPrimaryKey1 = 1000;
  const int kPrimaryKey2 = 1001;
  autofill::PasswordForm form1 = MakePasswordForm(kSignonRealm1);
  autofill::PasswordForm form2 = MakePasswordForm(kSignonRealm2);

  fake_db()->AddLoginForPrimaryKey(kPrimaryKey1, form1);
  fake_db()->AddLoginForPrimaryKey(kPrimaryKey2, form2);

  std::unique_ptr<syncer::DataBatch> batch;

  bridge()->GetAllDataForDebugging(base::BindLambdaForTesting(
      [&](std::unique_ptr<syncer::DataBatch> in_batch) {
        batch = std::move(in_batch);
      }));

  ASSERT_THAT(batch, NotNull());
  EXPECT_TRUE(batch->HasNext());
  while (batch->HasNext()) {
    const syncer::KeyAndData& data_pair = batch->Next();
    EXPECT_EQ("hidden", data_pair.second->specifics.password()
                            .client_only_encrypted_data()
                            .password_value());
  }
}

TEST_F(PasswordSyncBridgeTest,
       ShouldCallModelReadyUponConstructionWithMetadata) {
  ON_CALL(*mock_sync_metadata_store_sync(), GetAllSyncMetadata())
      .WillByDefault([&]() {
        sync_pb::ModelTypeState model_type_state;
        model_type_state.set_initial_sync_done(true);
        auto metadata_batch = std::make_unique<syncer::MetadataBatch>();
        metadata_batch->SetModelTypeState(model_type_state);
        metadata_batch->AddMetadata("storage_key", sync_pb::EntityMetadata());
        return metadata_batch;
      });

  EXPECT_CALL(mock_processor(), ModelReadyToSync(MetadataBatchContains(
                                    /*state=*/syncer::HasInitialSyncDone(),
                                    /*entities=*/testing::SizeIs(1))));

  PasswordSyncBridge bridge(mock_processor().CreateForwardingProcessor(),
                            mock_password_store_sync());
}

}  // namespace password_manager