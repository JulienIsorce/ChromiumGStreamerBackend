// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "blimp/net/common.h"
#include "blimp/net/packet_writer.h"
#include "blimp/net/test_common.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/socket/socket.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Mock;
using testing::NotNull;
using testing::Return;
using testing::SaveArg;

namespace blimp {
namespace {

class PacketWriterTest : public testing::Test {
 public:
  PacketWriterTest()
      : test_data_(
            new net::DrainableIOBuffer(new net::StringIOBuffer(test_data_str_),
                                       test_data_str_.size())),
        message_writer_(&socket_) {}

  ~PacketWriterTest() override {}

 protected:
  const std::string test_data_str_ = "U WOT M8";
  scoped_refptr<net::DrainableIOBuffer> test_data_;

  MockStreamSocket socket_;
  PacketWriter message_writer_;
  base::MessageLoop message_loop_;
  testing::InSequence mock_sequence_;

 private:
  DISALLOW_COPY_AND_ASSIGN(PacketWriterTest);
};

// Successful write with 1 async header write and 1 async payload write.
TEST_F(PacketWriterTest, TestWriteAsync) {
  net::TestCompletionCallback writer_cb;
  net::CompletionCallback header_cb;
  net::CompletionCallback payload_cb;

  // Write header.
  EXPECT_CALL(socket_, Write(BufferEquals(EncodeHeader(test_data_str_.size())),
                             kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(SaveArg<2>(&header_cb), Return(net::ERR_IO_PENDING)));
  EXPECT_EQ(net::ERR_IO_PENDING,
            message_writer_.WritePacket(test_data_, writer_cb.callback()));
  Mock::VerifyAndClearExpectations(&socket_);

  // Write payload.
  EXPECT_CALL(socket_,
              Write(BufferEquals(test_data_str_), test_data_str_.size(), _))
      .WillOnce(DoAll(SaveArg<2>(&payload_cb), Return(net::ERR_IO_PENDING)));
  header_cb.Run(kPacketHeaderSizeBytes);
  Mock::VerifyAndClearExpectations(&socket_);

  payload_cb.Run(test_data_str_.size());
  EXPECT_EQ(net::OK, writer_cb.WaitForResult());
}

// Successful write with 2 async header writes and 2 async payload writes.
TEST_F(PacketWriterTest, TestPartialWriteAsync) {
  net::TestCompletionCallback writer_cb;
  net::CompletionCallback header_cb;
  net::CompletionCallback payload_cb;

  std::string header = EncodeHeader(test_data_str_.size());
  std::string payload = test_data_str_;

  EXPECT_CALL(socket_, Write(BufferEquals(header), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(SaveArg<2>(&header_cb), Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();
  EXPECT_CALL(socket_, Write(BufferEquals(header.substr(1, header.size())),
                             header.size() - 1, _))
      .WillOnce(DoAll(SaveArg<2>(&header_cb), Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();
  EXPECT_CALL(socket_, Write(BufferEquals(payload), payload.size(), _))
      .WillOnce(DoAll(SaveArg<2>(&payload_cb), Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();
  EXPECT_CALL(socket_,
              Write(BufferEquals(payload.substr(1, payload.size() - 1)),
                    payload.size() - 1, _))
      .WillOnce(DoAll(SaveArg<2>(&payload_cb), Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();

  EXPECT_EQ(net::ERR_IO_PENDING,
            message_writer_.WritePacket(test_data_, writer_cb.callback()));

  // Header is written - first one byte, then the remainder.
  header_cb.Run(1);
  header_cb.Run(header.size() - 1);

  // Payload is written - first one byte, then the remainder.
  payload_cb.Run(1);
  payload_cb.Run(payload.size() - 1);

  EXPECT_EQ(net::OK, writer_cb.WaitForResult());
}

// Async socket error while writing data.
TEST_F(PacketWriterTest, TestWriteErrorAsync) {
  net::TestCompletionCallback writer_cb;
  net::CompletionCallback header_cb;
  net::CompletionCallback payload_cb;

  EXPECT_CALL(socket_, Write(BufferEquals(EncodeHeader(test_data_str_.size())),
                             kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(SaveArg<2>(&header_cb), Return(net::ERR_IO_PENDING)));
  EXPECT_CALL(socket_,
              Write(BufferEquals(test_data_str_), test_data_str_.size(), _))
      .WillOnce(DoAll(SaveArg<2>(&payload_cb), Return(net::ERR_IO_PENDING)));

  EXPECT_EQ(net::ERR_IO_PENDING,
            message_writer_.WritePacket(test_data_, writer_cb.callback()));
  header_cb.Run(kPacketHeaderSizeBytes);
  payload_cb.Run(net::ERR_CONNECTION_RESET);

  EXPECT_EQ(net::ERR_CONNECTION_RESET, writer_cb.WaitForResult());
}

// Successful write with 1 sync header write and 1 sync payload write.
TEST_F(PacketWriterTest, TestWriteSync) {
  net::TestCompletionCallback writer_cb;
  EXPECT_CALL(socket_, Write(BufferEquals(EncodeHeader(test_data_str_.size())),
                             kPacketHeaderSizeBytes, _))
      .WillOnce(Return(kPacketHeaderSizeBytes));
  EXPECT_CALL(socket_,
              Write(BufferEquals(test_data_str_), test_data_str_.size(), _))
      .WillOnce(Return(test_data_str_.size()));
  EXPECT_EQ(net::OK,
            message_writer_.WritePacket(test_data_, writer_cb.callback()));
  EXPECT_FALSE(writer_cb.have_result());
}

// Successful write with 2 sync header writes and 2 sync payload writes.
TEST_F(PacketWriterTest, TestPartialWriteSync) {
  net::TestCompletionCallback writer_cb;

  std::string header = EncodeHeader(test_data_str_.size());
  std::string payload = test_data_str_;

  EXPECT_CALL(socket_, Write(BufferEquals(header), header.size(), _))
      .WillOnce(Return(1));
  EXPECT_CALL(socket_, Write(BufferEquals(header.substr(1, header.size() - 1)),
                             header.size() - 1, _))
      .WillOnce(Return(header.size() - 1));
  EXPECT_CALL(socket_, Write(BufferEquals(payload), payload.size(), _))
      .WillOnce(Return(1));
  EXPECT_CALL(socket_, Write(BufferEquals(payload.substr(1, payload.size())),
                             payload.size() - 1, _))
      .WillOnce(Return(payload.size() - 1));

  EXPECT_EQ(net::OK,
            message_writer_.WritePacket(test_data_, writer_cb.callback()));
  EXPECT_FALSE(writer_cb.have_result());
}

// Verify that zero-length packets are rejected.
TEST_F(PacketWriterTest, TestZeroLengthPacketsRejected) {
  net::TestCompletionCallback writer_cb;

  EXPECT_EQ(net::ERR_INVALID_ARGUMENT,
            message_writer_.WritePacket(
                new net::DrainableIOBuffer(new net::IOBuffer(0), 0),
                writer_cb.callback()));

  EXPECT_FALSE(writer_cb.have_result());
}

// Sync socket error while writing header data.
TEST_F(PacketWriterTest, TestWriteHeaderErrorSync) {
  net::TestCompletionCallback writer_cb;

  EXPECT_CALL(socket_, Write(BufferEquals(EncodeHeader(test_data_str_.size())),
                             kPacketHeaderSizeBytes, _))
      .WillOnce(Return(net::ERR_FAILED));

  EXPECT_EQ(net::ERR_FAILED,
            message_writer_.WritePacket(test_data_, writer_cb.callback()));

  EXPECT_EQ(net::ERR_EMPTY_RESPONSE,
            writer_cb.GetResult(net::ERR_EMPTY_RESPONSE));
  EXPECT_FALSE(writer_cb.have_result());
}

// Sync socket error while writing payload data.
TEST_F(PacketWriterTest, TestWritePayloadErrorSync) {
  net::TestCompletionCallback writer_cb;

  EXPECT_CALL(socket_, Write(BufferEquals(EncodeHeader(test_data_str_.size())),
                             kPacketHeaderSizeBytes, _))
      .WillOnce(Return(kPacketHeaderSizeBytes));
  EXPECT_CALL(socket_,
              Write(BufferEquals(test_data_str_), test_data_str_.size(), _))
      .WillOnce(Return(net::ERR_FAILED));

  EXPECT_EQ(net::ERR_FAILED,
            message_writer_.WritePacket(test_data_, writer_cb.callback()));
  EXPECT_FALSE(writer_cb.have_result());
}

// Verify that asynchronous header write completions don't cause a
// use-after-free error if the writer object is deleted.
TEST_F(PacketWriterTest, DeletedDuringHeaderWrite) {
  net::TestCompletionCallback writer_cb;
  net::CompletionCallback header_cb;
  net::CompletionCallback payload_cb;
  scoped_ptr<PacketWriter> writer(new PacketWriter(&socket_));

  // Write header.
  EXPECT_CALL(socket_, Write(BufferEquals(EncodeHeader(test_data_str_.size())),
                             kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(SaveArg<2>(&header_cb), Return(net::ERR_IO_PENDING)));
  EXPECT_EQ(net::ERR_IO_PENDING,
            writer->WritePacket(test_data_, writer_cb.callback()));
  Mock::VerifyAndClearExpectations(&socket_);

  // Header write completion callback is invoked after the writer died.
  writer.reset();
  header_cb.Run(kPacketHeaderSizeBytes);
}

// Verify that asynchronous payload write completions don't cause a
// use-after-free error if the writer object is deleted.
TEST_F(PacketWriterTest, DeletedDuringPayloadWrite) {
  net::TestCompletionCallback writer_cb;
  net::CompletionCallback header_cb;
  net::CompletionCallback payload_cb;
  scoped_ptr<PacketWriter> writer(new PacketWriter(&socket_));

  EXPECT_CALL(socket_, Write(BufferEquals(EncodeHeader(test_data_str_.size())),
                             kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(SaveArg<2>(&header_cb), Return(net::ERR_IO_PENDING)));
  EXPECT_CALL(socket_,
              Write(BufferEquals(test_data_str_), test_data_str_.size(), _))
      .WillOnce(DoAll(SaveArg<2>(&payload_cb), Return(net::ERR_IO_PENDING)));

  EXPECT_EQ(net::ERR_IO_PENDING,
            writer->WritePacket(test_data_, writer_cb.callback()));

  // Header write completes successfully.
  header_cb.Run(kPacketHeaderSizeBytes);

  // Payload write completion callback is invoked after the writer died.
  writer.reset();
  payload_cb.Run(test_data_str_.size());
}

}  // namespace
}  // namespace blimp
