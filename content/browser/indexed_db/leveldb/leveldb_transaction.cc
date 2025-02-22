// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/indexed_db/leveldb/leveldb_transaction.h"

#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/time/time.h"
#include "content/browser/indexed_db/indexed_db_tracing.h"
#include "content/browser/indexed_db/leveldb/leveldb_database.h"
#include "content/browser/indexed_db/leveldb/leveldb_write_batch.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"

using base::StringPiece;

namespace content {

LevelDBTransaction::LevelDBTransaction(LevelDBDatabase* db)
    : db_(db),
      snapshot_(db),
      comparator_(db->Comparator()),
      data_comparator_(comparator_),
      data_(data_comparator_),
      finished_(false) {}

LevelDBTransaction::Record::Record() : deleted(false) {}
LevelDBTransaction::Record::~Record() {}

void LevelDBTransaction::Clear() {
  for (const auto& it : data_)
    delete it.second;
  data_.clear();
}

LevelDBTransaction::~LevelDBTransaction() { Clear(); }

void LevelDBTransaction::Set(const StringPiece& key,
                             std::string* value,
                             bool deleted) {
  DCHECK(!finished_);
  DataType::iterator it = data_.find(key);

  if (it == data_.end()) {
    Record* record = new Record();
    record->key.assign(key.begin(), key.end() - key.begin());
    record->value.swap(*value);
    record->deleted = deleted;
    data_[record->key] = record;
    NotifyIterators();
    return;
  }

  it->second->value.swap(*value);
  it->second->deleted = deleted;
}

void LevelDBTransaction::Put(const StringPiece& key, std::string* value) {
  Set(key, value, false);
}

void LevelDBTransaction::Remove(const StringPiece& key) {
  std::string empty;
  Set(key, &empty, true);
}

leveldb::Status LevelDBTransaction::Get(const StringPiece& key,
                                        std::string* value,
                                        bool* found) {
  *found = false;
  DCHECK(!finished_);
  std::string string_key(key.begin(), key.end() - key.begin());
  DataType::const_iterator it = data_.find(string_key);

  if (it != data_.end()) {
    if (it->second->deleted)
      return leveldb::Status::OK();

    *value = it->second->value;
    *found = true;
    return leveldb::Status::OK();
  }

  leveldb::Status s = db_->Get(key, value, found, &snapshot_);
  if (!s.ok())
    DCHECK(!*found);
  return s;
}

leveldb::Status LevelDBTransaction::Commit() {
  DCHECK(!finished_);
  IDB_TRACE("LevelDBTransaction::Commit");

  if (data_.empty()) {
    finished_ = true;
    return leveldb::Status::OK();
  }

  base::TimeTicks begin_time = base::TimeTicks::Now();
  scoped_ptr<LevelDBWriteBatch> write_batch = LevelDBWriteBatch::Create();

  auto it = data_.begin(), end = data_.end();
  while (it != end) {
    if (!it->second->deleted)
      write_batch->Put(it->first, it->second->value);
    else
      write_batch->Remove(it->first);

    delete it->second;
    data_.erase(it++);
  }

  DCHECK(data_.empty());

  leveldb::Status s = db_->Write(*write_batch);
  if (s.ok()) {
    finished_ = true;
    UMA_HISTOGRAM_TIMES("WebCore.IndexedDB.LevelDB.Transaction.CommitTime",
                         base::TimeTicks::Now() - begin_time);
  }
  return s;
}

void LevelDBTransaction::Rollback() {
  DCHECK(!finished_);
  finished_ = true;
  Clear();
}

scoped_ptr<LevelDBIterator> LevelDBTransaction::CreateIterator() {
  return TransactionIterator::Create(this);
}

scoped_ptr<LevelDBTransaction::DataIterator>
LevelDBTransaction::DataIterator::Create(LevelDBTransaction* transaction) {
  return make_scoped_ptr(new DataIterator(transaction));
}

bool LevelDBTransaction::DataIterator::IsValid() const {
  return iterator_ != data_->end();
}

leveldb::Status LevelDBTransaction::DataIterator::SeekToLast() {
  iterator_ = data_->end();
  if (iterator_ != data_->begin())
    --iterator_;
  return leveldb::Status::OK();
}

leveldb::Status LevelDBTransaction::DataIterator::Seek(
    const StringPiece& target) {
  iterator_ = data_->lower_bound(target);
  return leveldb::Status::OK();
}

leveldb::Status LevelDBTransaction::DataIterator::Next() {
  DCHECK(IsValid());
  ++iterator_;
  return leveldb::Status::OK();
}

leveldb::Status LevelDBTransaction::DataIterator::Prev() {
  DCHECK(IsValid());
  if (iterator_ != data_->begin())
    --iterator_;
  else
    iterator_ = data_->end();
  return leveldb::Status::OK();
}

StringPiece LevelDBTransaction::DataIterator::Key() const {
  DCHECK(IsValid());
  return iterator_->first;
}

StringPiece LevelDBTransaction::DataIterator::Value() const {
  DCHECK(IsValid());
  DCHECK(!IsDeleted());
  return iterator_->second->value;
}

bool LevelDBTransaction::DataIterator::IsDeleted() const {
  DCHECK(IsValid());
  return iterator_->second->deleted;
}

LevelDBTransaction::DataIterator::~DataIterator() {}

LevelDBTransaction::DataIterator::DataIterator(LevelDBTransaction* transaction)
    : data_(&transaction->data_),
      iterator_(data_->end()) {}

scoped_ptr<LevelDBTransaction::TransactionIterator>
LevelDBTransaction::TransactionIterator::Create(
    scoped_refptr<LevelDBTransaction> transaction) {
  return make_scoped_ptr(new TransactionIterator(transaction));
}

LevelDBTransaction::TransactionIterator::TransactionIterator(
    scoped_refptr<LevelDBTransaction> transaction)
    : transaction_(transaction),
      comparator_(transaction_->comparator_),
      data_iterator_(DataIterator::Create(transaction_.get())),
      db_iterator_(transaction_->db_->CreateIterator(&transaction_->snapshot_)),
      current_(0),
      direction_(FORWARD),
      data_changed_(false) {
  transaction_->RegisterIterator(this);
}

LevelDBTransaction::TransactionIterator::~TransactionIterator() {
  transaction_->UnregisterIterator(this);
}

bool LevelDBTransaction::TransactionIterator::IsValid() const {
  return !!current_;
}

leveldb::Status LevelDBTransaction::TransactionIterator::SeekToLast() {
  leveldb::Status s = data_iterator_->SeekToLast();
  DCHECK(s.ok());
  s = db_iterator_->SeekToLast();
  if (!s.ok())
    return s;
  direction_ = REVERSE;

  HandleConflictsAndDeletes();
  SetCurrentIteratorToLargestKey();
  return s;
}

leveldb::Status LevelDBTransaction::TransactionIterator::Seek(
    const StringPiece& target) {
  leveldb::Status s = data_iterator_->Seek(target);
  DCHECK(s.ok());
  s = db_iterator_->Seek(target);
  if (!s.ok())
    return s;
  direction_ = FORWARD;

  HandleConflictsAndDeletes();
  SetCurrentIteratorToSmallestKey();
  return s;
}

leveldb::Status LevelDBTransaction::TransactionIterator::Next() {
  DCHECK(IsValid());
  if (data_changed_)
    RefreshDataIterator();

  leveldb::Status s;
  if (direction_ != FORWARD) {
    // Ensure the non-current iterator is positioned after Key().

    LevelDBIterator* non_current = (current_ == db_iterator_.get())
                                       ? data_iterator_.get()
                                       : db_iterator_.get();

    non_current->Seek(Key());
    if (non_current->IsValid() &&
        !comparator_->Compare(non_current->Key(), Key())) {
      // Take an extra step so the non-current key is
      // strictly greater than Key().
      s = non_current->Next();
      if (!s.ok())
        return s;
    }
    DCHECK(!non_current->IsValid() ||
           comparator_->Compare(non_current->Key(), Key()) > 0);

    direction_ = FORWARD;
  }

  s = current_->Next();
  if (!s.ok())
    return s;
  HandleConflictsAndDeletes();
  SetCurrentIteratorToSmallestKey();
  return leveldb::Status::OK();
}

leveldb::Status LevelDBTransaction::TransactionIterator::Prev() {
  DCHECK(IsValid());
  leveldb::Status s;
  if (data_changed_)
    RefreshDataIterator();

  if (direction_ != REVERSE) {
    // Ensure the non-current iterator is positioned before Key().

    LevelDBIterator* non_current = (current_ == db_iterator_.get())
                                       ? data_iterator_.get()
                                       : db_iterator_.get();

    s = non_current->Seek(Key());
    if (!s.ok())
      return s;
    if (non_current->IsValid()) {
      // Iterator is at first entry >= Key().
      // Step back once to entry < key.
      // This is why we don't check for the keys being the same before
      // stepping, like we do in Next() above.
      non_current->Prev();
    } else {
      // Iterator has no entries >= Key(). Position at last entry.
      non_current->SeekToLast();
    }
    DCHECK(!non_current->IsValid() ||
           comparator_->Compare(non_current->Key(), Key()) < 0);

    direction_ = REVERSE;
  }

  s = current_->Prev();
  if (!s.ok())
    return s;
  HandleConflictsAndDeletes();
  SetCurrentIteratorToLargestKey();
  return leveldb::Status::OK();
}

StringPiece LevelDBTransaction::TransactionIterator::Key() const {
  DCHECK(IsValid());
  if (data_changed_)
    RefreshDataIterator();
  return current_->Key();
}

StringPiece LevelDBTransaction::TransactionIterator::Value() const {
  DCHECK(IsValid());
  if (data_changed_)
    RefreshDataIterator();
  return current_->Value();
}

void LevelDBTransaction::TransactionIterator::DataChanged() {
  data_changed_ = true;
}

void LevelDBTransaction::TransactionIterator::RefreshDataIterator() const {
  DCHECK(data_changed_);

  data_changed_ = false;

  if (data_iterator_->IsValid() && data_iterator_.get() == current_) {
    return;
  }

  if (db_iterator_->IsValid()) {
    // There could be new records in data that we should iterate over.

    if (direction_ == FORWARD) {
      // Try to seek data iterator to something greater than the db iterator.
      data_iterator_->Seek(db_iterator_->Key());
      if (data_iterator_->IsValid() &&
          !comparator_->Compare(data_iterator_->Key(), db_iterator_->Key())) {
        // If equal, take another step so the data iterator is strictly greater.
        data_iterator_->Next();
      }
    } else {
      // If going backward, seek to a key less than the db iterator.
      DCHECK_EQ(REVERSE, direction_);
      data_iterator_->Seek(db_iterator_->Key());
      if (data_iterator_->IsValid())
        data_iterator_->Prev();
    }
  }
}

bool LevelDBTransaction::TransactionIterator::DataIteratorIsLower() const {
  return comparator_->Compare(data_iterator_->Key(), db_iterator_->Key()) < 0;
}

bool LevelDBTransaction::TransactionIterator::DataIteratorIsHigher() const {
  return comparator_->Compare(data_iterator_->Key(), db_iterator_->Key()) > 0;
}

void LevelDBTransaction::TransactionIterator::HandleConflictsAndDeletes() {
  bool loop = true;

  while (loop) {
    loop = false;

    if (data_iterator_->IsValid() && db_iterator_->IsValid() &&
        !comparator_->Compare(data_iterator_->Key(), db_iterator_->Key())) {
      // For equal keys, the data iterator takes precedence, so move the
      // database iterator another step.
      if (direction_ == FORWARD)
        db_iterator_->Next();
      else
        db_iterator_->Prev();
    }

    // Skip over delete markers in the data iterator until it catches up with
    // the db iterator.
    if (data_iterator_->IsValid() && data_iterator_->IsDeleted()) {
      if (direction_ == FORWARD &&
          (!db_iterator_->IsValid() || DataIteratorIsLower())) {
        data_iterator_->Next();
        loop = true;
      } else if (direction_ == REVERSE &&
                 (!db_iterator_->IsValid() || DataIteratorIsHigher())) {
        data_iterator_->Prev();
        loop = true;
      }
    }
  }
}

void
LevelDBTransaction::TransactionIterator::SetCurrentIteratorToSmallestKey() {
  LevelDBIterator* smallest = 0;

  if (data_iterator_->IsValid())
    smallest = data_iterator_.get();

  if (db_iterator_->IsValid()) {
    if (!smallest ||
        comparator_->Compare(db_iterator_->Key(), smallest->Key()) < 0)
      smallest = db_iterator_.get();
  }

  current_ = smallest;
}

void LevelDBTransaction::TransactionIterator::SetCurrentIteratorToLargestKey() {
  LevelDBIterator* largest = 0;

  if (data_iterator_->IsValid())
    largest = data_iterator_.get();

  if (db_iterator_->IsValid()) {
    if (!largest ||
        comparator_->Compare(db_iterator_->Key(), largest->Key()) > 0)
      largest = db_iterator_.get();
  }

  current_ = largest;
}

void LevelDBTransaction::RegisterIterator(TransactionIterator* iterator) {
  DCHECK(iterators_.find(iterator) == iterators_.end());
  iterators_.insert(iterator);
}

void LevelDBTransaction::UnregisterIterator(TransactionIterator* iterator) {
  DCHECK(iterators_.find(iterator) != iterators_.end());
  iterators_.erase(iterator);
}

void LevelDBTransaction::NotifyIterators() {
  for (auto* transaction_iterator : iterators_)
    transaction_iterator->DataChanged();
}

scoped_ptr<LevelDBDirectTransaction> LevelDBDirectTransaction::Create(
    LevelDBDatabase* db) {
  return make_scoped_ptr(new LevelDBDirectTransaction(db));
}

LevelDBDirectTransaction::LevelDBDirectTransaction(LevelDBDatabase* db)
    : db_(db), write_batch_(LevelDBWriteBatch::Create()), finished_(false) {}

LevelDBDirectTransaction::~LevelDBDirectTransaction() {
  write_batch_->Clear();
}

void LevelDBDirectTransaction::Put(const StringPiece& key,
                                   const std::string* value) {
  DCHECK(!finished_);
  write_batch_->Put(key, *value);
}

leveldb::Status LevelDBDirectTransaction::Get(const StringPiece& key,
                                              std::string* value,
                                              bool* found) {
  *found = false;
  DCHECK(!finished_);

  leveldb::Status s = db_->Get(key, value, found);
  DCHECK(s.ok() || !*found);
  return s;
}

void LevelDBDirectTransaction::Remove(const StringPiece& key) {
  DCHECK(!finished_);
  write_batch_->Remove(key);
}

leveldb::Status LevelDBDirectTransaction::Commit() {
  DCHECK(!finished_);
  IDB_TRACE("LevelDBDirectTransaction::Commit");

  leveldb::Status s = db_->Write(*write_batch_);
  if (s.ok()) {
    finished_ = true;
    write_batch_->Clear();
  }
  return s;
}

}  // namespace content
