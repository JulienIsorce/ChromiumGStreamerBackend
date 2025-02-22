// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/callback.h"
#include "base/run_loop.h"
#include "base/thread_task_runner_handle.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/memory_dump_provider.h"
#include "base/trace_event/trace_event.h"
#include "components/tracing/child_memory_dump_manager_delegate_impl.h"
#include "components/tracing/child_trace_message_filter.h"
#include "components/tracing/tracing_messages.h"
#include "content/public/test/render_view_test.h"
#include "testing/gmock/include/gmock/gmock.h"

using base::trace_event::MemoryDumpManager;
using base::trace_event::MemoryDumpRequestArgs;
using base::trace_event::MemoryDumpArgs;
using base::trace_event::MemoryDumpLevelOfDetail;
using base::trace_event::MemoryDumpType;
using testing::_;
using testing::Return;

namespace tracing {

// A mock dump provider, used to check that dump requests actually end up
// creating memory dumps.
class MockDumpProvider : public base::trace_event::MemoryDumpProvider {
 public:
  MOCK_METHOD2(OnMemoryDump,
               bool(const MemoryDumpArgs& args,
                    base::trace_event::ProcessMemoryDump* pmd));
};

class ChildTracingTest : public content::RenderViewTest {
 public:
  // Used as callback argument for MemoryDumpManager::RequestGlobalDump():
  void OnMemoryDumpCallback(uint64 dump_guid, bool status) {
    last_callback_dump_guid_ = dump_guid;
    last_callback_status_ = status;
    ++callback_call_count_;
  }

 protected:
  void SetUp() override {
    // RenderViewTest::SetUp causes additional registrations, so we first
    // register the mock dump provider and ignore registrations from then on.
    // In addition to the mock dump provider, the TraceLog has already
    // registered itself by now; this cannot be prevented easily.
    mock_dump_provider_.reset(new MockDumpProvider());
    MemoryDumpManager::GetInstance()->RegisterDumpProvider(
        mock_dump_provider_.get());
    MemoryDumpManager::GetInstance()
        ->set_dumper_registrations_ignored_for_testing(true);

    RenderViewTest::SetUp();

    callback_call_count_ = 0;
    last_callback_dump_guid_ = 0;
    last_callback_status_ = false;
    callback_ = base::Bind(&ChildTracingTest::OnMemoryDumpCallback,
                           base::Unretained(this));
    task_runner_ = base::ThreadTaskRunnerHandle::Get();
    ctmf_ = make_scoped_refptr(new ChildTraceMessageFilter(task_runner_.get()));
    render_thread_->AddFilter(ctmf_.get());

    // Getting an instance of |ChildMemoryDumpManagerDelegateImpl| calls
    // |MemoryDumpManager::Initialize| with the correct delegate.
    ChildMemoryDumpManagerDelegateImpl::GetInstance();
  }

  void TearDown() override {
    RenderViewTest::TearDown();
    MemoryDumpManager::GetInstance()->UnregisterDumpProvider(
        mock_dump_provider_.get());
    mock_dump_provider_.reset();
    ctmf_ = nullptr;
    task_runner_ = nullptr;
  }

  // Simulates a synthetic browser -> child (this process) IPC message.
  void SimulateSyntheticMessageFromBrowser(const IPC::Message& msg) {
    task_runner_->PostTask(
        FROM_HERE, base::Bind(base::IgnoreResult(
                                  &ChildTraceMessageFilter::OnMessageReceived),
                              ctmf_, msg));
    base::RunLoop().RunUntilIdle();
  }

  void EnableTracingWithMemoryDumps() {
    std::string category_filter = "-*,";  // Disable all other trace categories.
    category_filter += MemoryDumpManager::kTraceCategory;
    base::trace_event::TraceConfig trace_config(category_filter, "");
    TracingMsg_BeginTracing msg(trace_config.ToString(), base::TraceTicks(), 0);
    SimulateSyntheticMessageFromBrowser(msg);
  }

  void DisableTracing() {
    SimulateSyntheticMessageFromBrowser(TracingMsg_EndTracing());
  }

  // Simulates a synthetic browser -> child process memory dump request and
  // checks that the child actually sends a response to that.
  void RequestProcessMemoryDumpAndCheckResponse(uint64 dump_guid) {
    SimulateSyntheticMessageFromBrowser(TracingMsg_ProcessMemoryDumpRequest(
        {dump_guid, MemoryDumpType::EXPLICITLY_TRIGGERED}));

    // Check that a child -> browser response to the local dump request is sent.
    const IPC::Message* msg = render_thread_->sink().GetUniqueMessageMatching(
        TracingHostMsg_ProcessMemoryDumpResponse::ID);
    EXPECT_NE(nullptr, msg);

    // Check that the |dump_guid| and the |sucess| fields are properly set.
    TracingHostMsg_ProcessMemoryDumpResponse::Param params;
    TracingHostMsg_ProcessMemoryDumpResponse::Read(msg, &params);
    const uint64 resp_guid = base::get<0>(params);
    const bool resp_success = base::get<1>(params);
    EXPECT_EQ(dump_guid, resp_guid);
    EXPECT_TRUE(resp_success);
  }

  // Retrieves the MemoryDumpRequestArgs of the global memory dump request that
  // this child process tried to send to the browser. Fails if either none or
  // multiple requests were sent.
  MemoryDumpRequestArgs GetInterceptedGlobalMemoryDumpRequest() {
    const IPC::Message* msg = render_thread_->sink().GetUniqueMessageMatching(
        TracingHostMsg_GlobalMemoryDumpRequest::ID);
    EXPECT_NE(nullptr, msg);
    TracingHostMsg_GlobalMemoryDumpRequest::Param params;
    TracingHostMsg_GlobalMemoryDumpRequest::Read(msg, &params);
    MemoryDumpRequestArgs args = base::get<0>(params);
    EXPECT_NE(0U, args.dump_guid);
    return args;
  }

  scoped_refptr<ChildTraceMessageFilter> ctmf_;
  scoped_ptr<MockDumpProvider> mock_dump_provider_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  base::trace_event::MemoryDumpCallback callback_;
  uint32 callback_call_count_;
  uint64 last_callback_dump_guid_;
  bool last_callback_status_;
};

// Covers the case of some browser-initiated memory dumps.
TEST_F(ChildTracingTest, BrowserInitiatedMemoryDumps) {
  const uint32 kNumDumps = 3;

  EnableTracingWithMemoryDumps();
  EXPECT_CALL(*mock_dump_provider_, OnMemoryDump(_, _))
      .Times(kNumDumps)
      .WillRepeatedly(Return(true));

  for (uint32 i = 0; i < kNumDumps; ++i) {
    render_thread_->sink().ClearMessages();
    RequestProcessMemoryDumpAndCheckResponse(i + 1);
  }

  DisableTracing();
}

// Covers the case of one simple child-initiated memory dump without callback,
// simulating a global memory dump request to the browser (+ response).
TEST_F(ChildTracingTest, SingleChildInitiatedMemoryDump) {
  EnableTracingWithMemoryDumps();

  // Expect that our mock dump provider is called when the emulated memory dump
  // request (browser -> child) is sent.
  EXPECT_CALL(*mock_dump_provider_, OnMemoryDump(_, _))
      .Times(1)
      .WillRepeatedly(Return(true));

  // Send the global memory dump request to the browser.
  render_thread_->sink().ClearMessages();
  MemoryDumpManager::GetInstance()->RequestGlobalDump(
      MemoryDumpType::EXPLICITLY_TRIGGERED, MemoryDumpLevelOfDetail::DETAILED);
  base::RunLoop().RunUntilIdle();

  // Check that the child -> browser global dump request IPC is actually sent.
  MemoryDumpRequestArgs args = GetInterceptedGlobalMemoryDumpRequest();
  EXPECT_EQ(MemoryDumpType::EXPLICITLY_TRIGGERED, args.dump_type);

  // Emulate a browser -> child process dump request and corresponding response.
  RequestProcessMemoryDumpAndCheckResponse(args.dump_guid);

  // Send a synthetic browser -> child global memory dump response.
  SimulateSyntheticMessageFromBrowser(
      TracingMsg_GlobalMemoryDumpResponse(args.dump_guid, true));

  DisableTracing();
}

// Covers the case of a global memory dump being requested while another one is
// in progress and has not been acknowledged by the browser. The second request
// is expected to fail immediately, while the first one is expected to suceed.
TEST_F(ChildTracingTest, OverlappingChildInitiatedMemoryDumps) {
  EnableTracingWithMemoryDumps();

  // Expect that our mock dump provider is called only once.
  EXPECT_CALL(*mock_dump_provider_, OnMemoryDump(_, _))
      .Times(1)
      .WillRepeatedly(Return(true));

  // Send the global memory dump request to the browser.
  render_thread_->sink().ClearMessages();
  MemoryDumpManager::GetInstance()->RequestGlobalDump(
      MemoryDumpType::EXPLICITLY_TRIGGERED, MemoryDumpLevelOfDetail::DETAILED,
      callback_);
  base::RunLoop().RunUntilIdle();

  // Check that the child -> browser global dump request IPC is actually sent.
  MemoryDumpRequestArgs args = GetInterceptedGlobalMemoryDumpRequest();
  EXPECT_EQ(MemoryDumpType::EXPLICITLY_TRIGGERED, args.dump_type);

  // Emulate a browser -> child process dump request and corresponding response.
  RequestProcessMemoryDumpAndCheckResponse(args.dump_guid);

  // Before the response for the first global dump is sent, send another one,
  // and expect that to fail.
  render_thread_->sink().ClearMessages();
  MemoryDumpManager::GetInstance()->RequestGlobalDump(
      MemoryDumpType::EXPLICITLY_TRIGGERED, MemoryDumpLevelOfDetail::DETAILED,
      callback_);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1u, callback_call_count_);
  EXPECT_FALSE(last_callback_status_);
  // Whatever the guid of the second failing request is, it cannot possibly be
  // equal to the guid of the first request.
  EXPECT_NE(args.dump_guid, last_callback_dump_guid_);

  // Also, check that no request has been forwarded to the browser (because the
  // first request is still outstanding and has not received any response yet).
  EXPECT_EQ(nullptr, render_thread_->sink().GetUniqueMessageMatching(
                         TracingHostMsg_GlobalMemoryDumpRequest::ID));

  // Now send a synthetic browser -> child response to the first request.
  SimulateSyntheticMessageFromBrowser(
      TracingMsg_GlobalMemoryDumpResponse(args.dump_guid, true));

  // Verify that the the callback for the first request is finally called.
  EXPECT_EQ(2u, callback_call_count_);
  EXPECT_EQ(args.dump_guid, last_callback_dump_guid_);
  EXPECT_TRUE(last_callback_status_);

  DisableTracing();
}

// Covers the case of five child-initiated global memory dumps. Each global dump
// request has a callback, which is expected to fail for 3 out of 5 cases.
TEST_F(ChildTracingTest, MultipleChildInitiatedMemoryDumpWithFailures) {
  const uint32 kNumRequests = 5;
  MemoryDumpType kDumpType = MemoryDumpType::EXPLICITLY_TRIGGERED;

  EnableTracingWithMemoryDumps();
  EXPECT_CALL(*mock_dump_provider_, OnMemoryDump(_, _))
      .Times(kNumRequests)
      .WillRepeatedly(Return(true));

  for (uint32 i = 0; i < kNumRequests; ++i) {
    render_thread_->sink().ClearMessages();
    MemoryDumpManager::GetInstance()->RequestGlobalDump(
        kDumpType, MemoryDumpLevelOfDetail::DETAILED, callback_);
    base::RunLoop().RunUntilIdle();

    MemoryDumpRequestArgs args = GetInterceptedGlobalMemoryDumpRequest();
    EXPECT_EQ(kDumpType, args.dump_type);
    RequestProcessMemoryDumpAndCheckResponse(args.dump_guid);

    const bool success = (i & 1) ? true : false;
    SimulateSyntheticMessageFromBrowser(
        TracingMsg_GlobalMemoryDumpResponse(args.dump_guid, success));
    EXPECT_EQ(i + 1, callback_call_count_);
    EXPECT_EQ(args.dump_guid, last_callback_dump_guid_);
    EXPECT_EQ(success, last_callback_status_);
  }

  DisableTracing();
}

}  // namespace tracing
