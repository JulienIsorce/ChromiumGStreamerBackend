// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <string>

#include "base/sys_byteorder.h"
#include "blimp/net/common.h"
#include "blimp/net/packet_reader.h"
#include "blimp/net/test_common.h"
#include "net/base/completion_callback.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/socket/socket.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::Mock;
using testing::NotNull;
using testing::Return;
using testing::SaveArg;
using testing::WithArg;

namespace blimp {
namespace {

const size_t kTestMaxBufferSize = 1 << 16;  // 64KB

class PacketReaderTest : public testing::Test {
 public:
  PacketReaderTest()
      : buffer_(new net::GrowableIOBuffer),
        test_msg_("U WOT M8"),
        data_reader_(&socket_) {
    buffer_->SetCapacity(kTestMaxBufferSize);
  }

  ~PacketReaderTest() override {}

  int ReadPacket() {
    return data_reader_.ReadPacket(buffer_, callback_.callback());
  }

 protected:
  scoped_refptr<net::GrowableIOBuffer> buffer_;
  std::string test_msg_;
  net::TestCompletionCallback callback_;
  testing::StrictMock<MockStreamSocket> socket_;
  testing::InSequence sequence_;
  PacketReader data_reader_;
};

// Successful read with 1 async header read and 1 async payload read.
TEST_F(PacketReaderTest, ReadAsyncHeaderAsyncPayload) {
  net::CompletionCallback socket_cb;

  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(test_msg_.size())),
                      SaveArg<2>(&socket_cb), Return(net::ERR_IO_PENDING)));
  EXPECT_CALL(socket_, Read(NotNull(), test_msg_.size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(test_msg_),
                      SaveArg<2>(&socket_cb), Return(net::ERR_IO_PENDING)));

  EXPECT_EQ(net::ERR_IO_PENDING, ReadPacket());
  socket_cb.Run(kPacketHeaderSizeBytes);
  socket_cb.Run(test_msg_.size());
  int rv = callback_.WaitForResult();

  EXPECT_EQ(rv, static_cast<int>(test_msg_.size()));
  EXPECT_TRUE(BufferStartsWith(buffer_.get(), test_msg_));
}

// Successful read with 1 async header read and 1 sync payload read.
TEST_F(PacketReaderTest, ReadAsyncHeaderSyncPayload) {
  net::CompletionCallback socket_cb;

  // Asynchronous payload read expectation.
  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(test_msg_.size())),
                      SaveArg<2>(&socket_cb), Return(net::ERR_IO_PENDING)));

  // Synchronous payload read expectation. Fills the buffer and returns
  // immediately.
  EXPECT_CALL(socket_, Read(NotNull(), test_msg_.size(), _))
      .WillOnce(
          DoAll(FillBufferFromString<0>(test_msg_), Return(test_msg_.size())));

  EXPECT_EQ(net::ERR_IO_PENDING, ReadPacket());
  EXPECT_FALSE(callback_.have_result());

  socket_cb.Run(kPacketHeaderSizeBytes);
  int rv = callback_.WaitForResult();
  EXPECT_GT(rv, 0);
  EXPECT_EQ(rv, static_cast<int>(test_msg_.size()));
  EXPECT_TRUE(BufferStartsWith(buffer_.get(), test_msg_));
}

// Successful read with 1 sync header read and 1 async payload read.
TEST_F(PacketReaderTest, ReadSyncHeaderAsyncPayload) {
  net::CompletionCallback socket_cb;

  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(test_msg_.size())),
                      Return(kPacketHeaderSizeBytes)));
  EXPECT_CALL(socket_, Read(NotNull(), test_msg_.size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(test_msg_),
                      SaveArg<2>(&socket_cb), Return(net::ERR_IO_PENDING)));

  EXPECT_EQ(net::ERR_IO_PENDING, ReadPacket());
  socket_cb.Run(test_msg_.size());
  int rv = callback_.WaitForResult();

  EXPECT_GT(rv, 0);
  EXPECT_EQ(rv, static_cast<int>(test_msg_.size()));
  EXPECT_TRUE(BufferStartsWith(buffer_.get(), test_msg_));
}

// Successful read with 1 sync header read and 1 sync payload read.
TEST_F(PacketReaderTest, ReadSyncHeaderSyncPayload) {
  net::CompletionCallback socket_cb;

  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(test_msg_.size())),
                      Return(kPacketHeaderSizeBytes)));
  EXPECT_CALL(socket_, Read(NotNull(), test_msg_.size(), _))
      .WillOnce(
          DoAll(FillBufferFromString<0>(test_msg_), Return(test_msg_.size())));

  int rv = ReadPacket();

  EXPECT_GT(rv, 0);
  EXPECT_EQ(rv, static_cast<int>(test_msg_.size()));
  EXPECT_TRUE(BufferStartsWith(buffer_.get(), test_msg_));
  EXPECT_FALSE(callback_.have_result());
}

// Successful read of 2 messages, header and payload reads all completing
// synchronously with no partial results.
TEST_F(PacketReaderTest, ReadMultipleMessagesSync) {
  net::CompletionCallback socket_cb;
  std::string test_msg2 = test_msg_ + "SlightlyLongerString";

  // Read the first message's header synchronously.
  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(test_msg_.size())),
                      Return(kPacketHeaderSizeBytes)))
      .RetiresOnSaturation();

  // Read the first message's payload synchronously.
  EXPECT_CALL(socket_, Read(NotNull(), test_msg_.size(), _))
      .WillOnce(
          DoAll(FillBufferFromString<0>(test_msg_), Return(test_msg_.size())))
      .RetiresOnSaturation();

  // Read the second message's header synchronously.
  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(test_msg2.size())),
                      Return(kPacketHeaderSizeBytes)))
      .RetiresOnSaturation();

  // Read the second message's payload synchronously.
  EXPECT_CALL(socket_, Read(NotNull(), test_msg2.size(), _))
      .WillOnce(
          DoAll(FillBufferFromString<0>(test_msg2), Return(test_msg2.size())))
      .RetiresOnSaturation();

  int rv = ReadPacket();

  EXPECT_GT(rv, 0);
  EXPECT_EQ(rv, static_cast<int>(test_msg_.size()));
  rv = ReadPacket();
  EXPECT_GT(rv, 0);
  EXPECT_EQ(rv, static_cast<int>(test_msg2.size()));
  EXPECT_TRUE(BufferStartsWith(buffer_.get(), test_msg_));
  EXPECT_FALSE(callback_.have_result());
}

// Successful read of 2 messages, header and payload reads all completing
// asynchronously with no partial results.
TEST_F(PacketReaderTest, ReadMultipleMessagesAsync) {
  net::TestCompletionCallback read_cb1;
  net::TestCompletionCallback read_cb2;
  net::CompletionCallback socket_cb;

  // Read a header asynchronously.
  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(test_msg_.size())),
                      SaveArg<2>(&socket_cb), Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();

  // Read a payload asynchronously.
  EXPECT_CALL(socket_, Read(NotNull(), test_msg_.size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(test_msg_),
                      SaveArg<2>(&socket_cb), Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();

  // Read a header asynchronously.
  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(test_msg_.size())),
                      SaveArg<2>(&socket_cb), Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();

  // Read a payload asynchronously.
  EXPECT_CALL(socket_, Read(NotNull(), test_msg_.size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(test_msg_),
                      SaveArg<2>(&socket_cb), Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();

  EXPECT_EQ(net::ERR_IO_PENDING,
            data_reader_.ReadPacket(buffer_, read_cb1.callback()));
  socket_cb.Run(kPacketHeaderSizeBytes);
  socket_cb.Run(test_msg_.size());
  int rv = read_cb1.WaitForResult();

  EXPECT_GT(rv, 0);
  EXPECT_EQ(rv, static_cast<int>(test_msg_.size()));

  EXPECT_EQ(net::ERR_IO_PENDING,
            data_reader_.ReadPacket(buffer_, read_cb2.callback()));
  socket_cb.Run(kPacketHeaderSizeBytes);
  socket_cb.Run(test_msg_.size());
  rv = read_cb2.WaitForResult();

  EXPECT_GT(rv, 0);
  EXPECT_EQ(rv, static_cast<int>(test_msg_.size()));
  EXPECT_TRUE(BufferStartsWith(buffer_.get(), test_msg_));
}

// Verify that partial header reads are supported.
// Read #0: 1 header byte is read.
// Read #1: Remainder of header bytes read.
TEST_F(PacketReaderTest, PartialHeaderReadAsync) {
  net::CompletionCallback cb;
  std::string header = EncodeHeader(test_msg_.size());

  // The first byte is received (sliced via substr()).
  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(header.substr(0, 1)),
                      SaveArg<2>(&cb), Return(net::ERR_IO_PENDING)));
  // The remainder is received (sliced via substr()).
  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes - 1, _))
      .WillOnce(DoAll(
          FillBufferFromString<0>(header.substr(1, kPacketHeaderSizeBytes - 1)),
          SaveArg<2>(&cb), Return(net::ERR_IO_PENDING)));
  // Verify that we start reading the body once the header has been fully read.
  EXPECT_CALL(socket_, Read(NotNull(), test_msg_.size(), _))
      .WillOnce(Return(net::ERR_IO_PENDING));

  EXPECT_EQ(net::ERR_IO_PENDING, ReadPacket());
  cb.Run(1);
  cb.Run(kPacketHeaderSizeBytes - 1);
}

// Verify that partial payload reads are supported.
// Read #0: Header is fully read synchronously.
// Read #1: First payload byte is read. (Um, it's an acoustic cup modem.)
// Read #2: Remainder of payload bytes are read.
TEST_F(PacketReaderTest, PartialPayloadReadAsync) {
  net::CompletionCallback cb;

  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(test_msg_.size())),
                      Return(kPacketHeaderSizeBytes)));
  EXPECT_CALL(socket_, Read(NotNull(), test_msg_.size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(test_msg_.substr(0, 1)),
                      SaveArg<2>(&cb), Return(net::ERR_IO_PENDING)));
  EXPECT_EQ(net::ERR_IO_PENDING, ReadPacket());
  EXPECT_CALL(socket_, Read(NotNull(), test_msg_.size() - 1, _))
      .WillOnce(DoAll(
          FillBufferFromString<0>(test_msg_.substr(1, test_msg_.size() - 1)),
          SaveArg<2>(&cb), Return(net::ERR_IO_PENDING)));

  cb.Run(1);
  cb.Run(test_msg_.size() - 1);
  int rv = callback_.WaitForResult();

  EXPECT_GT(rv, 0);
  EXPECT_EQ(static_cast<int>(test_msg_.size()), rv);
}

// Verify that synchronous header read errors are reported correctly.
TEST_F(PacketReaderTest, ReadHeaderErrorSync) {
  net::CompletionCallback cb;
  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(Return(net::ERR_FAILED));
  EXPECT_EQ(net::ERR_FAILED, ReadPacket());
}

// Verify that synchronous payload read errors are reported correctly.
TEST_F(PacketReaderTest, ReadPayloadErrorSync) {
  net::CompletionCallback cb;

  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(test_msg_.size())),
                      Return(kPacketHeaderSizeBytes)));
  EXPECT_CALL(socket_, Read(NotNull(), test_msg_.size(), _))
      .WillOnce(Return(net::ERR_FAILED));

  EXPECT_EQ(net::ERR_FAILED, ReadPacket());
}

// Verify that async header read errors are reported correctly.
TEST_F(PacketReaderTest, ReadHeaderErrorAsync) {
  net::CompletionCallback cb;
  net::TestCompletionCallback test_cb;

  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(test_msg_.size())),
                      SaveArg<2>(&cb), Return(net::ERR_IO_PENDING)));

  EXPECT_EQ(net::ERR_IO_PENDING, ReadPacket());
  cb.Run(net::ERR_FAILED);
  EXPECT_EQ(net::ERR_FAILED, callback_.WaitForResult());
}

// Verify that asynchronous paylod read errors are reported correctly.
TEST_F(PacketReaderTest, ReadPayloadErrorAsync) {
  net::CompletionCallback cb;

  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(test_msg_.size())),
                      Return(kPacketHeaderSizeBytes)));
  EXPECT_CALL(socket_, Read(NotNull(), test_msg_.size(), _))
      .WillOnce(DoAll(SaveArg<2>(&cb), Return(net::ERR_IO_PENDING)));

  EXPECT_EQ(net::ERR_IO_PENDING, ReadPacket());
  cb.Run(net::ERR_FAILED);
  EXPECT_EQ(net::ERR_FAILED, callback_.WaitForResult());
}

// Verify that async header read completions don't break us if the PacketReader
// object was destroyed.
TEST_F(PacketReaderTest, ReaderDeletedDuringAsyncHeaderRead) {
  net::CompletionCallback cb;
  net::TestCompletionCallback test_cb;
  scoped_ptr<PacketReader> reader(new PacketReader(&socket_));

  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(test_msg_.size())),
                      SaveArg<2>(&cb), Return(net::ERR_IO_PENDING)));

  EXPECT_EQ(net::ERR_IO_PENDING,
            reader->ReadPacket(buffer_, callback_.callback()));
  reader.reset();                  // Delete the reader object.
  cb.Run(kPacketHeaderSizeBytes);  // Complete the socket operation.
}

// Verify that async payload read completions don't break us if the PacketReader
// object was destroyed.
TEST_F(PacketReaderTest, ReaderDeletedDuringAsyncPayloadRead) {
  net::CompletionCallback cb;
  scoped_ptr<PacketReader> reader(new PacketReader(&socket_));

  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(test_msg_.size())),
                      Return(kPacketHeaderSizeBytes)));
  EXPECT_CALL(socket_, Read(NotNull(), test_msg_.size(), _))
      .WillOnce(DoAll(SaveArg<2>(&cb), Return(net::ERR_IO_PENDING)));
  EXPECT_EQ(net::ERR_IO_PENDING,
            reader->ReadPacket(buffer_, callback_.callback()));

  reader.reset();           // Delete the reader object.
  cb.Run(net::ERR_FAILED);  // Complete the socket operation.
}

// Verify that zero-length payload is reported as an erroneous input.
TEST_F(PacketReaderTest, ReadWhatIsThisAPacketForAnts) {
  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(DoAll(FillBufferFromString<0>(EncodeHeader(0)),
                      Return(kPacketHeaderSizeBytes)))
      .RetiresOnSaturation();

  EXPECT_EQ(net::ERR_INVALID_RESPONSE, ReadPacket());
}

// Verify that an illegally large payloads is reported as an erroneous inputs.
TEST_F(PacketReaderTest, ReadErrorIllegallyLargePayload) {
  EXPECT_CALL(socket_, Read(NotNull(), kPacketHeaderSizeBytes, _))
      .WillOnce(
          DoAll(FillBufferFromString<0>(EncodeHeader(kTestMaxBufferSize + 1)),
                Return(kPacketHeaderSizeBytes)));

  EXPECT_EQ(net::ERR_INVALID_RESPONSE, ReadPacket());
}

}  // namespace

}  // namespace blimp
