// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_repository_impl.h"

#include <fuchsia/inspect/deprecated/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>

#include <vector>

#include "gtest/gtest.h"
#include "peridot/lib/convert/convert.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/ledger_repository_factory_impl.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/storage/fake/fake_db.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/sync_coordinator/testing/fake_ledger_sync.h"
#include "src/ledger/bin/testing/fake_disk_cleanup_manager.h"
#include "src/ledger/bin/testing/inspect.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"
#include "src/lib/inspect_deprecated/deprecated/expose.h"
#include "src/lib/inspect_deprecated/hierarchy.h"
#include "src/lib/inspect_deprecated/inspect.h"
#include "src/lib/inspect_deprecated/testing/inspect.h"

namespace ledger {
namespace {

constexpr char kInspectPathComponent[] = "test_repository";
constexpr char kTestTopLevelNodeName[] = "top-level-of-test node";

using ::inspect_deprecated::testing::ChildrenMatch;
using ::inspect_deprecated::testing::MetricList;
using ::inspect_deprecated::testing::NameMatches;
using ::inspect_deprecated::testing::NodeMatches;
using ::inspect_deprecated::testing::UIntMetricIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;

// Constructs a Matcher to be matched against a test-owned Inspect object (the
// Inspect object to which the LedgerRepositoryImpl under test attaches a child)
// that validates that the matched object has a hierarchy with a node for the
// LedgerRepositoryImpl under test, a node named |kLedgersInspectPathComponent|
// under that, and a node for each of the given |ledger_names| under that.
::testing::Matcher<const inspect_deprecated::ObjectHierarchy&> HierarchyMatcher(
    const std::vector<std::string> ledger_names) {
  auto ledger_expectations =
      std::vector<::testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>();
  for (const std::string& ledger_name : ledger_names) {
    ledger_expectations.push_back(NodeMatches(NameMatches(ledger_name)));
  }
  return ChildrenMatch(ElementsAre(ChildrenMatch(
      ElementsAre(AllOf(NodeMatches(NameMatches(kLedgersInspectPathComponent.ToString())),
                        ChildrenMatch(ElementsAreArray(ledger_expectations)))))));
}

// Blocks the initialization of the Db. The call of |GetOrCreateDb| will not be finished, as the
// callback is not being called.
class BlockingFakeDbFactory : public storage::DbFactory {
 public:
  void GetOrCreateDb(ledger::DetachedPath db_path, DbFactory::OnDbNotFound on_db_not_found,
                     fit::function<void(Status, std::unique_ptr<storage::Db>)> callback) override {}
};

// Provides empty implementation of user-level synchronization. Helps to track ledger-level
// synchronization of pages.
class FakeUserSync : public sync_coordinator::UserSync {
 public:
  FakeUserSync() = default;
  FakeUserSync(const FakeUserSync&) = delete;
  FakeUserSync& operator=(const FakeUserSync&) = delete;
  ~FakeUserSync() override = default;

  // Returns the number of times synchronization was started for the given page.
  int GetSyncCallsCount(storage::PageId page_id) {
    return ledger_sync_ptr_->GetSyncCallsCount(page_id);
  }

  // UserSync:
  void Start() override {}

  void SetWatcher(sync_coordinator::SyncStateWatcher* watcher) override {}

  // Creates a FakeLedgerSync to allow tracking of the page synchronization.
  std::unique_ptr<sync_coordinator::LedgerSync> CreateLedgerSync(
      fxl::StringView app_id, encryption::EncryptionService* encryption_service) override {
    auto ledger_sync = std::make_unique<sync_coordinator::FakeLedgerSync>();
    ledger_sync_ptr_ = ledger_sync.get();
    return std::move(ledger_sync);
  }

 private:
  sync_coordinator::FakeLedgerSync* ledger_sync_ptr_;
};

class LedgerRepositoryImplTest : public TestWithEnvironment {
 public:
  LedgerRepositoryImplTest() {
    auto fake_page_eviction_manager = std::make_unique<FakeDiskCleanupManager>();
    disk_cleanup_manager_ = fake_page_eviction_manager.get();
    top_level_node_ = inspect_deprecated::Node(kTestTopLevelNodeName);
    attachment_node_ =
        top_level_node_.CreateChild(kSystemUnderTestAttachmentPointPathComponent.ToString());

    DetachedPath detached_path = DetachedPath(tmpfs_.root_fd());
    std::unique_ptr<storage::fake::FakeDbFactory> db_factory =
        std::make_unique<storage::fake::FakeDbFactory>(dispatcher());
    std::unique_ptr<PageUsageDb> db = std::make_unique<PageUsageDb>(
        environment_.clock(), db_factory.get(), detached_path.SubPath("page_usage_db"));
    auto background_sync_manager = std::make_unique<BackgroundSyncManager>(&environment_, db.get());

    auto user_sync = std::make_unique<FakeUserSync>();
    user_sync_ = user_sync.get();

    repository_ = std::make_unique<LedgerRepositoryImpl>(
        detached_path.SubPath("ledgers"), &environment_, std::move(db_factory), std::move(db),
        nullptr, std::move(user_sync), std::move(fake_page_eviction_manager),
        std::move(background_sync_manager), std::vector<PageUsageListener*>{disk_cleanup_manager_},
        attachment_node_.CreateChild(kInspectPathComponent));
  }

  ~LedgerRepositoryImplTest() override = default;

 protected:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  FakeDiskCleanupManager* disk_cleanup_manager_;
  FakeUserSync* user_sync_;
  // TODO(nathaniel): Because we use the ChildrenManager API, we need to do our
  // reads using FIDL, and because we want to use inspect_deprecated::ReadFromFidl for our
  // reads, we need to have these two objects (one parent, one child, both part
  // of the test, and with the system under test attaching to the child) rather
  // than just one. Even though this is test code this is still a layer of
  // indirection that should be eliminable in Inspect's upcoming "VMO-World".
  inspect_deprecated::Node top_level_node_;
  inspect_deprecated::Node attachment_node_;
  std::unique_ptr<LedgerRepositoryImpl> repository_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryImplTest);
};

TEST_F(LedgerRepositoryImplTest, ConcurrentCalls) {
  // Ensure the repository is not empty.
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;
  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // Make a first call to DiskCleanUp.
  bool callback_called1 = false;
  Status status1;
  repository_->DiskCleanUp(callback::Capture(callback::SetWhenCalled(&callback_called1), &status1));

  // Make a second one before the first one has finished.
  bool callback_called2 = false;
  Status status2;
  repository_->DiskCleanUp(callback::Capture(callback::SetWhenCalled(&callback_called2), &status2));

  // Make sure both of them start running.
  RunLoopUntilIdle();

  // Both calls must wait for the cleanup manager.
  EXPECT_FALSE(callback_called1);
  EXPECT_FALSE(callback_called2);

  // Call the cleanup manager callback and expect to see an ok status for both
  // pending callbacks.
  disk_cleanup_manager_->cleanup_callback(Status::OK);
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called1);
  EXPECT_TRUE(callback_called2);
  EXPECT_EQ(status1, Status::OK);
  EXPECT_EQ(status2, Status::OK);
}

TEST_F(LedgerRepositoryImplTest, InspectAPIRequestsMetricOnMultipleBindings) {
  // When nothing has bound to the repository, check that the "requests" metric
  // is present and is zero.
  inspect_deprecated::ObjectHierarchy zeroth_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &zeroth_hierarchy));
  EXPECT_THAT(zeroth_hierarchy, ChildrenMatch(Contains(NodeMatches(MetricList(Contains(UIntMetricIs(
                                    kRequestsInspectPathComponent.ToString(), 0UL)))))));

  // When one binding has been made to the repository, check that the "requests"
  // metric is present and is one.
  ledger_internal::LedgerRepositoryPtr first_ledger_repository_ptr;
  repository_->BindRepository(first_ledger_repository_ptr.NewRequest());
  inspect_deprecated::ObjectHierarchy first_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &first_hierarchy));
  EXPECT_THAT(first_hierarchy, ChildrenMatch(Contains(NodeMatches(MetricList(Contains(UIntMetricIs(
                                   kRequestsInspectPathComponent.ToString(), 1UL)))))));

  // When two bindings have been made to the repository, check that the
  // "requests" metric is present and is two.
  ledger_internal::LedgerRepositoryPtr second_ledger_repository_ptr;
  repository_->BindRepository(second_ledger_repository_ptr.NewRequest());
  inspect_deprecated::ObjectHierarchy second_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &second_hierarchy));
  EXPECT_THAT(second_hierarchy, ChildrenMatch(Contains(NodeMatches(MetricList(Contains(UIntMetricIs(
                                    kRequestsInspectPathComponent.ToString(), 2UL)))))));
}

TEST_F(LedgerRepositoryImplTest, InspectAPILedgerPresence) {
  std::string first_ledger_name = "first_ledger";
  std::string second_ledger_name = "second_ledger";
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;
  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // When nothing has requested a ledger, check that the Inspect hierarchy is as
  // expected with no nodes representing ledgers.
  inspect_deprecated::ObjectHierarchy zeroth_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &zeroth_hierarchy));
  EXPECT_THAT(zeroth_hierarchy, HierarchyMatcher({}));

  // When one ledger has been created in the repository, check that the Inspect
  // hierarchy is as expected with a node for that one ledger.
  ledger::LedgerPtr first_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(first_ledger_name),
                                   first_ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect_deprecated::ObjectHierarchy first_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &first_hierarchy));
  EXPECT_THAT(first_hierarchy, HierarchyMatcher({first_ledger_name}));

  // When two ledgers have been created in the repository, check that the
  // Inspect hierarchy is as expected with nodes for both ledgers.
  ledger::LedgerPtr second_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(second_ledger_name),
                                   second_ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect_deprecated::ObjectHierarchy second_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &second_hierarchy));
  EXPECT_THAT(second_hierarchy, HierarchyMatcher({first_ledger_name, second_ledger_name}));
}

TEST_F(LedgerRepositoryImplTest, InspectAPIDisconnectedLedgerPresence) {
  std::string first_ledger_name = "first_ledger";
  std::string second_ledger_name = "second_ledger";
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;
  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // When nothing has yet requested a ledger, check that the Inspect hierarchy
  // is as expected with no nodes representing ledgers.
  inspect_deprecated::ObjectHierarchy zeroth_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &zeroth_hierarchy));
  EXPECT_THAT(zeroth_hierarchy, HierarchyMatcher({}));

  // When one ledger has been created in the repository, check that the Inspect
  // hierarchy is as expected with a node for that one ledger.
  ledger::LedgerPtr first_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(first_ledger_name),
                                   first_ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect_deprecated::ObjectHierarchy hierarchy_after_one_connection;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &hierarchy_after_one_connection));
  EXPECT_THAT(hierarchy_after_one_connection, HierarchyMatcher({first_ledger_name}));

  // When two ledgers have been created in the repository, check that the
  // Inspect hierarchy is as expected with nodes for both ledgers.
  ledger::LedgerPtr second_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(second_ledger_name),
                                   second_ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect_deprecated::ObjectHierarchy hierarchy_after_two_connections;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &hierarchy_after_two_connections));
  EXPECT_THAT(hierarchy_after_two_connections,
              HierarchyMatcher({first_ledger_name, second_ledger_name}));

  first_ledger_ptr.Unbind();
  RunLoopUntilIdle();

  // When one of the two ledgers has been disconnected, check that an inspection
  // still finds both.
  inspect_deprecated::ObjectHierarchy hierarchy_after_one_disconnection;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &hierarchy_after_one_disconnection));
  EXPECT_THAT(hierarchy_after_one_disconnection,
              HierarchyMatcher({first_ledger_name, second_ledger_name}));

  second_ledger_ptr.Unbind();
  RunLoopUntilIdle();

  // When both of the ledgers have been disconnected, check that an inspection
  // still finds both.
  inspect_deprecated::ObjectHierarchy hierarchy_after_two_disconnections;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &hierarchy_after_two_disconnections));
  EXPECT_THAT(hierarchy_after_two_disconnections,
              HierarchyMatcher({first_ledger_name, second_ledger_name}));
}

// Verifies that closing a ledger repository closes the LedgerRepository
// connections once all Ledger connections are themselves closed.
TEST_F(LedgerRepositoryImplTest, Close) {
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr1;
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr2;
  ledger::LedgerPtr ledger_ptr;

  repository_->BindRepository(ledger_repository_ptr1.NewRequest());
  repository_->BindRepository(ledger_repository_ptr2.NewRequest());

  bool on_discardable_called;
  repository_->SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  bool ptr1_closed;
  zx_status_t ptr1_closed_status;
  ledger_repository_ptr1.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&ptr1_closed), &ptr1_closed_status));
  bool ptr2_closed;
  zx_status_t ptr2_closed_status;
  ledger_repository_ptr2.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&ptr2_closed), &ptr2_closed_status));
  bool ledger_closed;
  zx_status_t ledger_closed_status;
  ledger_ptr.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&ledger_closed), &ledger_closed_status));

  ledger_repository_ptr1->GetLedger(convert::ToArray("ledger"), ledger_ptr.NewRequest());
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
  EXPECT_FALSE(ptr1_closed);
  EXPECT_FALSE(ptr2_closed);
  EXPECT_FALSE(ledger_closed);

  ledger_repository_ptr2->Close();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
  EXPECT_FALSE(ptr1_closed);
  EXPECT_FALSE(ptr2_closed);
  EXPECT_FALSE(ledger_closed);

  ledger_ptr.Unbind();
  RunLoopUntilIdle();

  EXPECT_TRUE(on_discardable_called);
  EXPECT_FALSE(ptr1_closed);
  EXPECT_FALSE(ptr2_closed);

  // Delete the repository, as it would be done by LedgerRepositoryFactory when
  // the |on_discardable| callback is called.
  repository_.reset();
  RunLoopUntilIdle();
  EXPECT_TRUE(ptr1_closed);
  EXPECT_TRUE(ptr2_closed);

  EXPECT_EQ(ptr1_closed_status, ZX_OK);
  EXPECT_EQ(ptr2_closed_status, ZX_OK);
}

TEST_F(LedgerRepositoryImplTest, CloseEmpty) {
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr1;

  repository_->BindRepository(ledger_repository_ptr1.NewRequest());

  bool on_discardable_called;
  repository_->SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  bool ptr1_closed;
  zx_status_t ptr1_closed_status;
  ledger_repository_ptr1.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&ptr1_closed), &ptr1_closed_status));

  ledger_repository_ptr1->Close();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);

  // The connection is not closed by LedgerRepositoryImpl, but by its holder.
  EXPECT_FALSE(ptr1_closed);
}

TEST_F(LedgerRepositoryImplTest, CloseWhenEmptyWithoutCallback) {
  // Running will init the repo, that will trigger is emptyness check, which
  // will close it given that it has no user.
  RunLoopUntilIdle();

  bool called;
  Status status;
  repository_->GetLedger({}, nullptr, callback::Capture(callback::SetWhenCalled(&called), &status));
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::ILLEGAL_STATE);
}

// Verifies that the callback on closure is called, even if the on_discardable is not set.
TEST_F(LedgerRepositoryImplTest, CloseWithoutOnDiscardableCallback) {
  bool ptr1_closed;
  Status ptr1_closed_status;

  repository_->Close(callback::Capture(callback::SetWhenCalled(&ptr1_closed), &ptr1_closed_status));
  RunLoopUntilIdle();

  EXPECT_TRUE(ptr1_closed);
}

// Verifies that the object remains alive is the no on_discardable nor close_callback are
// set.
TEST_F(LedgerRepositoryImplTest, AliveWithNoCallbacksSet) {
  // Ensure the repository is not empty.
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;
  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // Make a first call to DiskCleanUp.
  bool callback_called1 = false;
  Status status1;
  repository_->DiskCleanUp(callback::Capture(callback::SetWhenCalled(&callback_called1), &status1));

  // Make sure it starts running.
  RunLoopUntilIdle();

  // The call must wait for the cleanup manager.
  EXPECT_FALSE(callback_called1);

  // Call the cleanup manager callback and expect to see an ok status for a pending callback.
  disk_cleanup_manager_->cleanup_callback(Status::OK);
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called1);
  EXPECT_EQ(status1, Status::OK);
}

// Verifies that the object is not destroyed until the initialization of PageUsageDb is finished.
TEST_F(LedgerRepositoryImplTest, CloseWhileDbInitRunning) {
  auto fake_page_eviction_manager = std::make_unique<FakeDiskCleanupManager>();
  auto disk_cleanup_manager = fake_page_eviction_manager.get();
  auto top_level_node = inspect_deprecated::Node(kTestTopLevelNodeName);
  auto attachment_node =
      top_level_node.CreateChild(kSystemUnderTestAttachmentPointPathComponent.ToString());

  DetachedPath detached_path = DetachedPath(tmpfs_.root_fd());
  // Makes sure that the initialization of PageusageDb will not be completed.
  std::unique_ptr<BlockingFakeDbFactory> db_factory = std::make_unique<BlockingFakeDbFactory>();
  std::unique_ptr<PageUsageDb> db = std::make_unique<PageUsageDb>(
      environment_.clock(), db_factory.get(), detached_path.SubPath("page_usage_database"));
  auto background_sync_manager = std::make_unique<BackgroundSyncManager>(&environment_, db.get());

  auto repository = std::make_unique<LedgerRepositoryImpl>(
      detached_path.SubPath("ledgers"), &environment_, std::move(db_factory), std::move(db),
      nullptr, nullptr, std::move(fake_page_eviction_manager), std::move(background_sync_manager),
      std::vector<PageUsageListener*>{disk_cleanup_manager},
      attachment_node.CreateChild(kInspectPathComponent));

  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr1;

  repository->BindRepository(ledger_repository_ptr1.NewRequest());

  bool on_discardable_called;
  repository->SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  bool ptr1_closed;
  zx_status_t ptr1_closed_status;
  ledger_repository_ptr1.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&ptr1_closed), &ptr1_closed_status));

  // The call should not trigger destruction, as the initialization of PageUsageDb is not finished.
  ledger_repository_ptr1->Close();
  RunLoopUntilIdle();
  EXPECT_FALSE(ptr1_closed);
}

PageId RandomId(const Environment& environment) {
  PageId result;
  environment.random()->Draw(&result.id);
  return result;
}

// Verifies that the LedgerRepositoryImpl triggers page sync for a page that exists and was closed.
TEST_F(LedgerRepositoryImplTest, TrySyncClosedPageSyncStarted) {
  PagePtr page;
  PageId id = RandomId(environment_);
  storage::PageId page_id = convert::ExtendedStringView(id.id).ToString();
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;

  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // Opens the Ledger and creates LedgerManager.
  std::string ledger_name = "ledger";
  ledger::LedgerPtr first_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(ledger_name), first_ledger_ptr.NewRequest());

  // Opens the page and starts the sync with the cloud for the first time.
  first_ledger_ptr->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(user_sync_->GetSyncCallsCount(page_id), 1);

  page.Unbind();
  RunLoopUntilIdle();

  // Starts the sync of the reopened page.
  repository_->TrySyncClosedPage(convert::ExtendedStringView(ledger_name),
                                 convert::ExtendedStringView(id.id));
  RunLoopUntilIdle();
  EXPECT_EQ(user_sync_->GetSyncCallsCount(page_id), 2);
}

// Verifies that the LedgerRepositoryImpl does not trigger the sync for a currently open page.
TEST_F(LedgerRepositoryImplTest, TrySyncClosedPageWithOpenedPage) {
  PagePtr page;
  PageId id = RandomId(environment_);
  storage::PageId page_id = convert::ExtendedStringView(id.id).ToString();
  ledger_internal::LedgerRepositoryPtr ledger_repository_ptr;

  repository_->BindRepository(ledger_repository_ptr.NewRequest());

  // Opens the Ledger and creates LedgerManager.
  std::string ledger_name = "ledger";
  ledger::LedgerPtr first_ledger_ptr;
  ledger_repository_ptr->GetLedger(convert::ToArray(ledger_name), first_ledger_ptr.NewRequest());

  // Opens the page and starts the sync with the cloud for the first time.
  first_ledger_ptr->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(user_sync_->GetSyncCallsCount(page_id), 1);

  // Tries to reopen the already-open page.
  repository_->TrySyncClosedPage(convert::ExtendedStringView(ledger_name),
                                 convert::ExtendedStringView(id.id));
  RunLoopUntilIdle();
  EXPECT_EQ(user_sync_->GetSyncCallsCount(page_id), 1);
}

}  // namespace
}  // namespace ledger
