// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipc/ipc_message_utils.h"

#include "base/files/file_path.h"
#include "base/json/json_writer.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/nullable_string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "ipc/ipc_channel_handle.h"
#include "ipc/ipc_message_attachment.h"
#include "ipc/ipc_message_attachment_set.h"

#if defined(OS_POSIX)
#include "ipc/ipc_platform_file_attachment_posix.h"
#endif

#if (defined(OS_MACOSX) && !defined(OS_IOS)) || defined(OS_WIN)
#include "base/memory/shared_memory_handle.h"
#endif  // (defined(OS_MACOSX) && !defined(OS_IOS)) || defined(OS_WIN)

#if defined(OS_MACOSX) && !defined(OS_IOS)
#include "ipc/mach_port_mac.h"
#endif

#if defined(OS_WIN)
#include <tchar.h>
#include "ipc/handle_win.h"
#endif

namespace IPC {

namespace {

const int kMaxRecursionDepth = 100;

template<typename CharType>
void LogBytes(const std::vector<CharType>& data, std::string* out) {
#if defined(OS_WIN)
  // Windows has a GUI for logging, which can handle arbitrary binary data.
  for (size_t i = 0; i < data.size(); ++i)
    out->push_back(data[i]);
#else
  // On POSIX, we log to stdout, which we assume can display ASCII.
  static const size_t kMaxBytesToLog = 100;
  for (size_t i = 0; i < std::min(data.size(), kMaxBytesToLog); ++i) {
    if (isprint(data[i]))
      out->push_back(data[i]);
    else
      out->append(
          base::StringPrintf("[%02X]", static_cast<unsigned char>(data[i])));
  }
  if (data.size() > kMaxBytesToLog) {
    out->append(base::StringPrintf(
        " and %u more bytes",
        static_cast<unsigned>(data.size() - kMaxBytesToLog)));
  }
#endif
}

bool ReadValue(const Message* m,
               base::PickleIterator* iter,
               base::Value** value,
               int recursion);

void WriteValue(Message* m, const base::Value* value, int recursion) {
  bool result;
  if (recursion > kMaxRecursionDepth) {
    LOG(WARNING) << "Max recursion depth hit in WriteValue.";
    return;
  }

  m->WriteInt(value->GetType());

  switch (value->GetType()) {
    case base::Value::TYPE_NULL:
    break;
    case base::Value::TYPE_BOOLEAN: {
      bool val;
      result = value->GetAsBoolean(&val);
      DCHECK(result);
      WriteParam(m, val);
      break;
    }
    case base::Value::TYPE_INTEGER: {
      int val;
      result = value->GetAsInteger(&val);
      DCHECK(result);
      WriteParam(m, val);
      break;
    }
    case base::Value::TYPE_DOUBLE: {
      double val;
      result = value->GetAsDouble(&val);
      DCHECK(result);
      WriteParam(m, val);
      break;
    }
    case base::Value::TYPE_STRING: {
      std::string val;
      result = value->GetAsString(&val);
      DCHECK(result);
      WriteParam(m, val);
      break;
    }
    case base::Value::TYPE_BINARY: {
      const base::BinaryValue* binary =
          static_cast<const base::BinaryValue*>(value);
      m->WriteData(binary->GetBuffer(), static_cast<int>(binary->GetSize()));
      break;
    }
    case base::Value::TYPE_DICTIONARY: {
      const base::DictionaryValue* dict =
          static_cast<const base::DictionaryValue*>(value);

      WriteParam(m, static_cast<int>(dict->size()));

      for (base::DictionaryValue::Iterator it(*dict); !it.IsAtEnd();
           it.Advance()) {
        WriteParam(m, it.key());
        WriteValue(m, &it.value(), recursion + 1);
      }
      break;
    }
    case base::Value::TYPE_LIST: {
      const base::ListValue* list = static_cast<const base::ListValue*>(value);
      WriteParam(m, static_cast<int>(list->GetSize()));
      for (base::ListValue::const_iterator it = list->begin();
           it != list->end(); ++it) {
        WriteValue(m, *it, recursion + 1);
      }
      break;
    }
  }
}

// Helper for ReadValue that reads a DictionaryValue into a pre-allocated
// object.
bool ReadDictionaryValue(const Message* m,
                         base::PickleIterator* iter,
                         base::DictionaryValue* value,
                         int recursion) {
  int size;
  if (!ReadParam(m, iter, &size))
    return false;

  for (int i = 0; i < size; ++i) {
    std::string key;
    base::Value* subval;
    if (!ReadParam(m, iter, &key) ||
        !ReadValue(m, iter, &subval, recursion + 1))
      return false;
    value->SetWithoutPathExpansion(key, subval);
  }

  return true;
}

// Helper for ReadValue that reads a ReadListValue into a pre-allocated
// object.
bool ReadListValue(const Message* m,
                   base::PickleIterator* iter,
                   base::ListValue* value,
                   int recursion) {
  int size;
  if (!ReadParam(m, iter, &size))
    return false;

  for (int i = 0; i < size; ++i) {
    base::Value* subval;
    if (!ReadValue(m, iter, &subval, recursion + 1))
      return false;
    value->Set(i, subval);
  }

  return true;
}

bool ReadValue(const Message* m,
               base::PickleIterator* iter,
               base::Value** value,
               int recursion) {
  if (recursion > kMaxRecursionDepth) {
    LOG(WARNING) << "Max recursion depth hit in ReadValue.";
    return false;
  }

  int type;
  if (!ReadParam(m, iter, &type))
    return false;

  switch (type) {
    case base::Value::TYPE_NULL:
      *value = base::Value::CreateNullValue().release();
    break;
    case base::Value::TYPE_BOOLEAN: {
      bool val;
      if (!ReadParam(m, iter, &val))
        return false;
      *value = new base::FundamentalValue(val);
      break;
    }
    case base::Value::TYPE_INTEGER: {
      int val;
      if (!ReadParam(m, iter, &val))
        return false;
      *value = new base::FundamentalValue(val);
      break;
    }
    case base::Value::TYPE_DOUBLE: {
      double val;
      if (!ReadParam(m, iter, &val))
        return false;
      *value = new base::FundamentalValue(val);
      break;
    }
    case base::Value::TYPE_STRING: {
      std::string val;
      if (!ReadParam(m, iter, &val))
        return false;
      *value = new base::StringValue(val);
      break;
    }
    case base::Value::TYPE_BINARY: {
      const char* data;
      int length;
      if (!iter->ReadData(&data, &length))
        return false;
      *value = base::BinaryValue::CreateWithCopiedBuffer(data, length);
      break;
    }
    case base::Value::TYPE_DICTIONARY: {
      scoped_ptr<base::DictionaryValue> val(new base::DictionaryValue());
      if (!ReadDictionaryValue(m, iter, val.get(), recursion))
        return false;
      *value = val.release();
      break;
    }
    case base::Value::TYPE_LIST: {
      scoped_ptr<base::ListValue> val(new base::ListValue());
      if (!ReadListValue(m, iter, val.get(), recursion))
        return false;
      *value = val.release();
      break;
    }
    default:
    return false;
  }

  return true;
}

}  // namespace

// -----------------------------------------------------------------------------

LogData::LogData()
    : routing_id(0),
      type(0),
      sent(0),
      receive(0),
      dispatch(0) {
}

LogData::~LogData() {
}

void ParamTraits<bool>::Log(const param_type& p, std::string* l) {
  l->append(p ? "true" : "false");
}

void ParamTraits<unsigned char>::Write(Message* m, const param_type& p) {
  m->WriteBytes(&p, sizeof(param_type));
}

bool ParamTraits<unsigned char>::Read(const Message* m,
                                      base::PickleIterator* iter,
                                      param_type* r) {
  const char* data;
  if (!iter->ReadBytes(&data, sizeof(param_type)))
    return false;
  memcpy(r, data, sizeof(param_type));
  return true;
}

void ParamTraits<unsigned char>::Log(const param_type& p, std::string* l) {
  l->append(base::UintToString(p));
}

void ParamTraits<unsigned short>::Write(Message* m, const param_type& p) {
  m->WriteBytes(&p, sizeof(param_type));
}

bool ParamTraits<unsigned short>::Read(const Message* m,
                                       base::PickleIterator* iter,
                                       param_type* r) {
  const char* data;
  if (!iter->ReadBytes(&data, sizeof(param_type)))
    return false;
  memcpy(r, data, sizeof(param_type));
  return true;
}

void ParamTraits<unsigned short>::Log(const param_type& p, std::string* l) {
  l->append(base::UintToString(p));
}

void ParamTraits<int>::Log(const param_type& p, std::string* l) {
  l->append(base::IntToString(p));
}

void ParamTraits<unsigned int>::Log(const param_type& p, std::string* l) {
  l->append(base::UintToString(p));
}

void ParamTraits<long>::Log(const param_type& p, std::string* l) {
  l->append(base::Int64ToString(static_cast<int64_t>(p)));
}

void ParamTraits<unsigned long>::Log(const param_type& p, std::string* l) {
  l->append(base::Uint64ToString(static_cast<uint64_t>(p)));
}

void ParamTraits<long long>::Log(const param_type& p, std::string* l) {
  l->append(base::Int64ToString(static_cast<int64_t>(p)));
}

void ParamTraits<unsigned long long>::Log(const param_type& p, std::string* l) {
  l->append(base::Uint64ToString(p));
}

void ParamTraits<float>::Log(const param_type& p, std::string* l) {
  l->append(base::StringPrintf("%e", p));
}

void ParamTraits<double>::Write(Message* m, const param_type& p) {
  m->WriteBytes(reinterpret_cast<const char*>(&p), sizeof(param_type));
}

bool ParamTraits<double>::Read(const Message* m,
                               base::PickleIterator* iter,
                               param_type* r) {
  const char *data;
  if (!iter->ReadBytes(&data, sizeof(*r))) {
    NOTREACHED();
    return false;
  }
  memcpy(r, data, sizeof(param_type));
  return true;
}

void ParamTraits<double>::Log(const param_type& p, std::string* l) {
  l->append(base::StringPrintf("%e", p));
}


void ParamTraits<std::string>::Log(const param_type& p, std::string* l) {
  l->append(p);
}

void ParamTraits<base::string16>::Log(const param_type& p, std::string* l) {
  l->append(base::UTF16ToUTF8(p));
}

void ParamTraits<std::vector<char> >::Write(Message* m, const param_type& p) {
  if (p.empty()) {
    m->WriteData(NULL, 0);
  } else {
    m->WriteData(&p.front(), static_cast<int>(p.size()));
  }
}

bool ParamTraits<std::vector<char>>::Read(const Message* m,
                                          base::PickleIterator* iter,
                                          param_type* r) {
  const char *data;
  int data_size = 0;
  if (!iter->ReadData(&data, &data_size) || data_size < 0)
    return false;
  r->resize(data_size);
  if (data_size)
    memcpy(&r->front(), data, data_size);
  return true;
}

void ParamTraits<std::vector<char> >::Log(const param_type& p, std::string* l) {
  LogBytes(p, l);
}

void ParamTraits<std::vector<unsigned char> >::Write(Message* m,
                                                     const param_type& p) {
  if (p.empty()) {
    m->WriteData(NULL, 0);
  } else {
    m->WriteData(reinterpret_cast<const char*>(&p.front()),
                 static_cast<int>(p.size()));
  }
}

bool ParamTraits<std::vector<unsigned char>>::Read(const Message* m,
                                                   base::PickleIterator* iter,
                                                   param_type* r) {
  const char *data;
  int data_size = 0;
  if (!iter->ReadData(&data, &data_size) || data_size < 0)
    return false;
  r->resize(data_size);
  if (data_size)
    memcpy(&r->front(), data, data_size);
  return true;
}

void ParamTraits<std::vector<unsigned char> >::Log(const param_type& p,
                                                   std::string* l) {
  LogBytes(p, l);
}

void ParamTraits<std::vector<bool> >::Write(Message* m, const param_type& p) {
  WriteParam(m, static_cast<int>(p.size()));
  // Cast to bool below is required because libc++'s
  // vector<bool>::const_reference is different from bool, and we want to avoid
  // writing an extra specialization of ParamTraits for it.
  for (size_t i = 0; i < p.size(); i++)
    WriteParam(m, static_cast<bool>(p[i]));
}

bool ParamTraits<std::vector<bool>>::Read(const Message* m,
                                          base::PickleIterator* iter,
                                          param_type* r) {
  int size;
  // ReadLength() checks for < 0 itself.
  if (!iter->ReadLength(&size))
    return false;
  r->resize(size);
  for (int i = 0; i < size; i++) {
    bool value;
    if (!ReadParam(m, iter, &value))
      return false;
    (*r)[i] = value;
  }
  return true;
}

void ParamTraits<std::vector<bool> >::Log(const param_type& p, std::string* l) {
  for (size_t i = 0; i < p.size(); ++i) {
    if (i != 0)
      l->push_back(' ');
    LogParam(static_cast<bool>(p[i]), l);
  }
}

void ParamTraits<BrokerableAttachment::AttachmentId>::Write(
    Message* m,
    const param_type& p) {
  m->WriteBytes(p.nonce, BrokerableAttachment::kNonceSize);
}

bool ParamTraits<BrokerableAttachment::AttachmentId>::Read(
    const Message* m,
    base::PickleIterator* iter,
    param_type* r) {
  const char* data;
  if (!iter->ReadBytes(&data, BrokerableAttachment::kNonceSize))
    return false;
  memcpy(r->nonce, data, BrokerableAttachment::kNonceSize);
  return true;
}

void ParamTraits<BrokerableAttachment::AttachmentId>::Log(const param_type& p,
                                                          std::string* l) {
  l->append(base::HexEncode(p.nonce, BrokerableAttachment::kNonceSize));
}

void ParamTraits<base::DictionaryValue>::Write(Message* m,
                                               const param_type& p) {
  WriteValue(m, &p, 0);
}

bool ParamTraits<base::DictionaryValue>::Read(const Message* m,
                                              base::PickleIterator* iter,
                                              param_type* r) {
  int type;
  if (!ReadParam(m, iter, &type) || type != base::Value::TYPE_DICTIONARY)
    return false;

  return ReadDictionaryValue(m, iter, r, 0);
}

void ParamTraits<base::DictionaryValue>::Log(const param_type& p,
                                             std::string* l) {
  std::string json;
  base::JSONWriter::Write(p, &json);
  l->append(json);
}

#if defined(OS_POSIX)
void ParamTraits<base::FileDescriptor>::Write(Message* m, const param_type& p) {
  const bool valid = p.fd >= 0;
  WriteParam(m, valid);

  if (!valid)
    return;

  if (p.auto_close) {
    if (!m->WriteAttachment(
            new internal::PlatformFileAttachment(base::ScopedFD(p.fd))))
      NOTREACHED();
  } else {
    if (!m->WriteAttachment(new internal::PlatformFileAttachment(p.fd)))
      NOTREACHED();
  }
}

bool ParamTraits<base::FileDescriptor>::Read(const Message* m,
                                             base::PickleIterator* iter,
                                             param_type* r) {
  *r = base::FileDescriptor();

  bool valid;
  if (!ReadParam(m, iter, &valid))
    return false;

  // TODO(morrita): Seems like this should return false.
  if (!valid)
    return true;

  scoped_refptr<MessageAttachment> attachment;
  if (!m->ReadAttachment(iter, &attachment))
    return false;

  *r = base::FileDescriptor(attachment->TakePlatformFile(), true);
  return true;
}

void ParamTraits<base::FileDescriptor>::Log(const param_type& p,
                                            std::string* l) {
  if (p.auto_close) {
    l->append(base::StringPrintf("FD(%d auto-close)", p.fd));
  } else {
    l->append(base::StringPrintf("FD(%d)", p.fd));
  }
}
#endif  // defined(OS_POSIX)

#if defined(OS_MACOSX) && !defined(OS_IOS)
void ParamTraits<base::SharedMemoryHandle>::Write(Message* m,
                                                  const param_type& p) {
  m->WriteInt(p.GetType());

  switch (p.GetType()) {
    case base::SharedMemoryHandle::POSIX:
      ParamTraits<base::FileDescriptor>::Write(m, p.GetFileDescriptor());
      break;
    case base::SharedMemoryHandle::MACH:
      MachPortMac mach_port_mac(p.GetMemoryObject());
      ParamTraits<MachPortMac>::Write(m, mach_port_mac);
      size_t size = 0;
      bool result = p.GetSize(&size);
      DCHECK(result);
      ParamTraits<size_t>::Write(m, size);

      // If the caller intended to pass ownership to the IPC stack, release a
      // reference.
      if (p.OwnershipPassesToIPC())
        p.Close();

      break;
  }
}

bool ParamTraits<base::SharedMemoryHandle>::Read(const Message* m,
                                                 base::PickleIterator* iter,
                                                 param_type* r) {
  base::SharedMemoryHandle::TypeWireFormat type;
  if (!iter->ReadInt(&type))
    return false;

  base::SharedMemoryHandle::Type shm_type = base::SharedMemoryHandle::POSIX;
  switch (type) {
    case base::SharedMemoryHandle::POSIX:
    case base::SharedMemoryHandle::MACH: {
      shm_type = static_cast<base::SharedMemoryHandle::Type>(type);
      break;
    }
    default: {
      return false;
    }
  }

  switch (shm_type) {
    case base::SharedMemoryHandle::POSIX: {
      base::FileDescriptor file_descriptor;

      bool success =
          ParamTraits<base::FileDescriptor>::Read(m, iter, &file_descriptor);
      if (!success)
        return false;

      *r = base::SharedMemoryHandle(file_descriptor.fd,
                                    file_descriptor.auto_close);
      return true;
    }
    case base::SharedMemoryHandle::MACH: {
      MachPortMac mach_port_mac;
      if (!ParamTraits<MachPortMac>::Read(m, iter, &mach_port_mac))
        return false;

      size_t size;
      if (!ParamTraits<size_t>::Read(m, iter, &size))
        return false;

      *r = base::SharedMemoryHandle(mach_port_mac.get_mach_port(), size,
                                    base::GetCurrentProcId());
      return true;
    }
  }
}

void ParamTraits<base::SharedMemoryHandle>::Log(const param_type& p,
                                                std::string* l) {
  switch (p.GetType()) {
    case base::SharedMemoryHandle::POSIX:
      l->append("POSIX Fd: ");
      ParamTraits<base::FileDescriptor>::Log(p.GetFileDescriptor(), l);
      break;
    case base::SharedMemoryHandle::MACH:
      l->append("Mach port: ");
      LogParam(p.GetMemoryObject(), l);
      break;
  }
}

#elif defined(OS_WIN)
void ParamTraits<base::SharedMemoryHandle>::Write(Message* m,
                                                  const param_type& p) {
  // Longs on windows are 32 bits.
  uint32_t pid = p.GetPID();
  m->WriteUInt32(pid);
  m->WriteBool(p.NeedsBrokering());

  if (p.NeedsBrokering()) {
    HandleWin handle_win(p.GetHandle(), HandleWin::DUPLICATE);
    ParamTraits<HandleWin>::Write(m, handle_win);
  } else {
    m->WriteInt(HandleToLong(p.GetHandle()));
  }
}

bool ParamTraits<base::SharedMemoryHandle>::Read(const Message* m,
                                                 base::PickleIterator* iter,
                                                 param_type* r) {
  uint32_t pid_int;
  if (!iter->ReadUInt32(&pid_int))
    return false;
  base::ProcessId pid = pid_int;

  bool needs_brokering;
  if (!iter->ReadBool(&needs_brokering))
    return false;

  if (needs_brokering) {
    HandleWin handle_win;
    if (!ParamTraits<HandleWin>::Read(m, iter, &handle_win))
      return false;
    *r = base::SharedMemoryHandle(handle_win.get_handle(), pid);
    return true;
  }

  int handle_int;
  if (!iter->ReadInt(&handle_int))
    return false;
  HANDLE handle = LongToHandle(handle_int);
  *r = base::SharedMemoryHandle(handle, pid);
  return true;
}

void ParamTraits<base::SharedMemoryHandle>::Log(const param_type& p,
                                                std::string* l) {
  LogParam(p.GetPID(), l);
  l->append(" ");
  LogParam(p.GetHandle(), l);
  l->append(" needs brokering: ");
  LogParam(p.NeedsBrokering(), l);
}
#endif  // defined(OS_MACOSX) && !defined(OS_IOS)

void ParamTraits<base::FilePath>::Write(Message* m, const param_type& p) {
  p.WriteToPickle(m);
}

bool ParamTraits<base::FilePath>::Read(const Message* m,
                                       base::PickleIterator* iter,
                                       param_type* r) {
  return r->ReadFromPickle(iter);
}

void ParamTraits<base::FilePath>::Log(const param_type& p, std::string* l) {
  ParamTraits<base::FilePath::StringType>::Log(p.value(), l);
}

void ParamTraits<base::ListValue>::Write(Message* m, const param_type& p) {
  WriteValue(m, &p, 0);
}

bool ParamTraits<base::ListValue>::Read(const Message* m,
                                        base::PickleIterator* iter,
                                        param_type* r) {
  int type;
  if (!ReadParam(m, iter, &type) || type != base::Value::TYPE_LIST)
    return false;

  return ReadListValue(m, iter, r, 0);
}

void ParamTraits<base::ListValue>::Log(const param_type& p, std::string* l) {
  std::string json;
  base::JSONWriter::Write(p, &json);
  l->append(json);
}

void ParamTraits<base::NullableString16>::Write(Message* m,
                                                const param_type& p) {
  WriteParam(m, p.string());
  WriteParam(m, p.is_null());
}

bool ParamTraits<base::NullableString16>::Read(const Message* m,
                                               base::PickleIterator* iter,
                                               param_type* r) {
  base::string16 string;
  if (!ReadParam(m, iter, &string))
    return false;
  bool is_null;
  if (!ReadParam(m, iter, &is_null))
    return false;
  *r = base::NullableString16(string, is_null);
  return true;
}

void ParamTraits<base::NullableString16>::Log(const param_type& p,
                                              std::string* l) {
  l->append("(");
  LogParam(p.string(), l);
  l->append(", ");
  LogParam(p.is_null(), l);
  l->append(")");
}

void ParamTraits<base::File::Info>::Write(Message* m,
                                          const param_type& p) {
  WriteParam(m, p.size);
  WriteParam(m, p.is_directory);
  WriteParam(m, p.last_modified.ToDoubleT());
  WriteParam(m, p.last_accessed.ToDoubleT());
  WriteParam(m, p.creation_time.ToDoubleT());
}

bool ParamTraits<base::File::Info>::Read(const Message* m,
                                         base::PickleIterator* iter,
                                         param_type* p) {
  double last_modified, last_accessed, creation_time;
  if (!ReadParam(m, iter, &p->size) ||
      !ReadParam(m, iter, &p->is_directory) ||
      !ReadParam(m, iter, &last_modified) ||
      !ReadParam(m, iter, &last_accessed) ||
      !ReadParam(m, iter, &creation_time))
    return false;
  p->last_modified = base::Time::FromDoubleT(last_modified);
  p->last_accessed = base::Time::FromDoubleT(last_accessed);
  p->creation_time = base::Time::FromDoubleT(creation_time);
  return true;
}

void ParamTraits<base::File::Info>::Log(const param_type& p,
                                        std::string* l) {
  l->append("(");
  LogParam(p.size, l);
  l->append(",");
  LogParam(p.is_directory, l);
  l->append(",");
  LogParam(p.last_modified.ToDoubleT(), l);
  l->append(",");
  LogParam(p.last_accessed.ToDoubleT(), l);
  l->append(",");
  LogParam(p.creation_time.ToDoubleT(), l);
  l->append(")");
}

void ParamTraits<base::Time>::Write(Message* m, const param_type& p) {
  ParamTraits<int64_t>::Write(m, p.ToInternalValue());
}

bool ParamTraits<base::Time>::Read(const Message* m,
                                   base::PickleIterator* iter,
                                   param_type* r) {
  int64_t value;
  if (!ParamTraits<int64_t>::Read(m, iter, &value))
    return false;
  *r = base::Time::FromInternalValue(value);
  return true;
}

void ParamTraits<base::Time>::Log(const param_type& p, std::string* l) {
  ParamTraits<int64_t>::Log(p.ToInternalValue(), l);
}

void ParamTraits<base::TimeDelta>::Write(Message* m, const param_type& p) {
  ParamTraits<int64_t>::Write(m, p.ToInternalValue());
}

bool ParamTraits<base::TimeDelta>::Read(const Message* m,
                                        base::PickleIterator* iter,
                                        param_type* r) {
  int64_t value;
  bool ret = ParamTraits<int64_t>::Read(m, iter, &value);
  if (ret)
    *r = base::TimeDelta::FromInternalValue(value);

  return ret;
}

void ParamTraits<base::TimeDelta>::Log(const param_type& p, std::string* l) {
  ParamTraits<int64_t>::Log(p.ToInternalValue(), l);
}

void ParamTraits<base::TimeTicks>::Write(Message* m, const param_type& p) {
  ParamTraits<int64_t>::Write(m, p.ToInternalValue());
}

bool ParamTraits<base::TimeTicks>::Read(const Message* m,
                                        base::PickleIterator* iter,
                                        param_type* r) {
  int64_t value;
  bool ret = ParamTraits<int64_t>::Read(m, iter, &value);
  if (ret)
    *r = base::TimeTicks::FromInternalValue(value);

  return ret;
}

void ParamTraits<base::TimeTicks>::Log(const param_type& p, std::string* l) {
  ParamTraits<int64_t>::Log(p.ToInternalValue(), l);
}

void ParamTraits<base::TraceTicks>::Write(Message* m, const param_type& p) {
  ParamTraits<int64_t>::Write(m, p.ToInternalValue());
}

bool ParamTraits<base::TraceTicks>::Read(const Message* m,
                                         base::PickleIterator* iter,
                                         param_type* r) {
  int64_t value;
  bool ret = ParamTraits<int64_t>::Read(m, iter, &value);
  if (ret)
    *r = base::TraceTicks::FromInternalValue(value);

  return ret;
}

void ParamTraits<base::TraceTicks>::Log(const param_type& p, std::string* l) {
  ParamTraits<int64_t>::Log(p.ToInternalValue(), l);
}

void ParamTraits<IPC::ChannelHandle>::Write(Message* m, const param_type& p) {
#if defined(OS_WIN)
  // On Windows marshalling pipe handle is not supported.
  DCHECK(p.pipe.handle == NULL);
#endif  // defined (OS_WIN)
  WriteParam(m, p.name);
#if defined(OS_POSIX)
  WriteParam(m, p.socket);
#endif
}

bool ParamTraits<IPC::ChannelHandle>::Read(const Message* m,
                                           base::PickleIterator* iter,
                                           param_type* r) {
  return ReadParam(m, iter, &r->name)
#if defined(OS_POSIX)
      && ReadParam(m, iter, &r->socket)
#endif
      ;
}

void ParamTraits<IPC::ChannelHandle>::Log(const param_type& p,
                                          std::string* l) {
  l->append(base::StringPrintf("ChannelHandle(%s", p.name.c_str()));
#if defined(OS_POSIX)
  l->append(", ");
  ParamTraits<base::FileDescriptor>::Log(p.socket, l);
#endif
  l->append(")");
}

void ParamTraits<LogData>::Write(Message* m, const param_type& p) {
  WriteParam(m, p.channel);
  WriteParam(m, p.routing_id);
  WriteParam(m, p.type);
  WriteParam(m, p.flags);
  WriteParam(m, p.sent);
  WriteParam(m, p.receive);
  WriteParam(m, p.dispatch);
  WriteParam(m, p.message_name);
  WriteParam(m, p.params);
}

bool ParamTraits<LogData>::Read(const Message* m,
                                base::PickleIterator* iter,
                                param_type* r) {
  return
      ReadParam(m, iter, &r->channel) &&
      ReadParam(m, iter, &r->routing_id) &&
      ReadParam(m, iter, &r->type) &&
      ReadParam(m, iter, &r->flags) &&
      ReadParam(m, iter, &r->sent) &&
      ReadParam(m, iter, &r->receive) &&
      ReadParam(m, iter, &r->dispatch) &&
      ReadParam(m, iter, &r->message_name) &&
      ReadParam(m, iter, &r->params);
}

void ParamTraits<LogData>::Log(const param_type& p, std::string* l) {
  // Doesn't make sense to implement this!
}

void ParamTraits<Message>::Write(Message* m, const Message& p) {
#if defined(OS_POSIX)
  // We don't serialize the file descriptors in the nested message, so there
  // better not be any.
  DCHECK(!p.HasAttachments());
#endif

  // Don't just write out the message. This is used to send messages between
  // NaCl (Posix environment) and the browser (could be on Windows). The message
  // header formats differ between these systems (so does handle sharing, but
  // we already asserted we don't have any handles). So just write out the
  // parts of the header we use.
  //
  // Be careful also to use only explicitly-sized types. The NaCl environment
  // could be 64-bit and the host browser could be 32-bits. The nested message
  // may or may not be safe to send between 32-bit and 64-bit systems, but we
  // leave that up to the code sending the message to ensure.
  m->WriteUInt32(static_cast<uint32_t>(p.routing_id()));
  m->WriteUInt32(p.type());
  m->WriteUInt32(p.flags());
  m->WriteData(p.payload(), static_cast<uint32_t>(p.payload_size()));
}

bool ParamTraits<Message>::Read(const Message* m,
                                base::PickleIterator* iter,
                                Message* r) {
  uint32_t routing_id, type, flags;
  if (!iter->ReadUInt32(&routing_id) ||
      !iter->ReadUInt32(&type) ||
      !iter->ReadUInt32(&flags))
    return false;

  int payload_size;
  const char* payload;
  if (!iter->ReadData(&payload, &payload_size))
    return false;

  r->SetHeaderValues(static_cast<int32_t>(routing_id), type, flags);
  return r->WriteBytes(payload, payload_size);
}

void ParamTraits<Message>::Log(const Message& p, std::string* l) {
  l->append("<IPC::Message>");
}

#if defined(OS_WIN)
// Note that HWNDs/HANDLE/HCURSOR/HACCEL etc are always 32 bits, even on 64
// bit systems. That's why we use the Windows macros to convert to 32 bits.
void ParamTraits<HANDLE>::Write(Message* m, const param_type& p) {
  m->WriteInt(HandleToLong(p));
}

bool ParamTraits<HANDLE>::Read(const Message* m,
                               base::PickleIterator* iter,
                               param_type* r) {
  int32_t temp;
  if (!iter->ReadInt(&temp))
    return false;
  *r = LongToHandle(temp);
  return true;
}

void ParamTraits<HANDLE>::Log(const param_type& p, std::string* l) {
  l->append(base::StringPrintf("0x%p", p));
}

void ParamTraits<LOGFONT>::Write(Message* m, const param_type& p) {
  m->WriteData(reinterpret_cast<const char*>(&p), sizeof(LOGFONT));
}

bool ParamTraits<LOGFONT>::Read(const Message* m,
                                base::PickleIterator* iter,
                                param_type* r) {
  const char *data;
  int data_size = 0;
  if (iter->ReadData(&data, &data_size) && data_size == sizeof(LOGFONT)) {
    const LOGFONT *font = reinterpret_cast<LOGFONT*>(const_cast<char*>(data));
    if (_tcsnlen(font->lfFaceName, LF_FACESIZE) < LF_FACESIZE) {
      memcpy(r, data, sizeof(LOGFONT));
      return true;
    }
  }

  NOTREACHED();
  return false;
}

void ParamTraits<LOGFONT>::Log(const param_type& p, std::string* l) {
  l->append(base::StringPrintf("<LOGFONT>"));
}

void ParamTraits<MSG>::Write(Message* m, const param_type& p) {
  m->WriteData(reinterpret_cast<const char*>(&p), sizeof(MSG));
}

bool ParamTraits<MSG>::Read(const Message* m,
                            base::PickleIterator* iter,
                            param_type* r) {
  const char *data;
  int data_size = 0;
  bool result = iter->ReadData(&data, &data_size);
  if (result && data_size == sizeof(MSG)) {
    memcpy(r, data, sizeof(MSG));
  } else {
    result = false;
    NOTREACHED();
  }

  return result;
}

void ParamTraits<MSG>::Log(const param_type& p, std::string* l) {
  l->append("<MSG>");
}

#endif  // OS_WIN

}  // namespace IPC
