// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/sync/remove_performer.h"

#include "base/sequenced_task_runner.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/file_system/operation_observer.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#include "chrome/browser/chromeos/drive/job_scheduler.h"
#include "chrome/browser/chromeos/drive/resource_entry_conversion.h"
#include "chrome/browser/chromeos/drive/sync/entry_revert_performer.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace drive {
namespace internal {

namespace {

// Updates local metadata and after remote unparenting.
FileError UpdateLocalStateAfterUnparent(ResourceMetadata* metadata,
                                        const std::string& local_id) {
  ResourceEntry entry;
  FileError error = metadata->GetResourceEntryById(local_id, &entry);
  if (error != FILE_ERROR_OK)
    return error;
  entry.set_parent_local_id(util::kDriveOtherDirLocalId);
  return metadata->RefreshEntry(entry);
}

// Utility function to run ResourceMetadata::RemoveEntry from UI thread.
void RemoveEntryOnUIThread(base::SequencedTaskRunner* blocking_task_runner,
                           ResourceMetadata* metadata,
                           const std::string& local_id,
                           const FileOperationCallback& callback) {
  base::PostTaskAndReplyWithResult(
      blocking_task_runner,
      FROM_HERE,
      base::Bind(&ResourceMetadata::RemoveEntry,
                 base::Unretained(metadata), local_id),
      callback);
}

}  // namespace

RemovePerformer::RemovePerformer(
    base::SequencedTaskRunner* blocking_task_runner,
    file_system::OperationObserver* observer,
    JobScheduler* scheduler,
    ResourceMetadata* metadata)
    : blocking_task_runner_(blocking_task_runner),
      observer_(observer),
      scheduler_(scheduler),
      metadata_(metadata),
      entry_revert_performer_(new EntryRevertPerformer(blocking_task_runner,
                                                       observer,
                                                       scheduler,
                                                       metadata)),
      weak_ptr_factory_(this) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

RemovePerformer::~RemovePerformer() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

void RemovePerformer::Remove(const std::string& local_id,
                             const ClientContext& context,
                             const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  ResourceEntry* entry = new ResourceEntry;
  base::PostTaskAndReplyWithResult(
      blocking_task_runner_.get(),
      FROM_HERE,
      base::Bind(&ResourceMetadata::GetResourceEntryById,
                 base::Unretained(metadata_),
                 local_id,
                 entry),
      base::Bind(&RemovePerformer::RemoveAfterGetResourceEntry,
                 weak_ptr_factory_.GetWeakPtr(),
                 context,
                 callback,
                 base::Owned(entry)));
}

void RemovePerformer::RemoveAfterGetResourceEntry(
    const ClientContext& context,
    const FileOperationCallback& callback,
    const ResourceEntry* entry,
    FileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != FILE_ERROR_OK) {
    callback.Run(error);
    return;
  }

  // To match with the behavior of drive.google.com:
  // Removal of shared entries under MyDrive is just removing from the parent.
  // The entry will stay in shared-with-me (in other words, in "drive/other".)
  //
  // TODO(kinaba): to be more precise, we might be better to branch by whether
  // or not the current account is an owner of the file. The code below is
  // written under the assumption that |shared_with_me| coincides with that.
  if (entry->shared_with_me()) {
    UnparentResource(context, callback, entry->resource_id(),
                     entry->local_id());
  } else {
    // Otherwise try sending the entry to trash.
    TrashResource(context, callback, entry->resource_id(), entry->local_id());
  }
}

void RemovePerformer::TrashResource(const ClientContext& context,
                                    const FileOperationCallback& callback,
                                    const std::string& resource_id,
                                    const std::string& local_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  scheduler_->TrashResource(
      resource_id,
      context,
      base::Bind(&RemovePerformer::TrashResourceAfterUpdateRemoteState,
                 weak_ptr_factory_.GetWeakPtr(), context, callback, local_id));
}

void RemovePerformer::TrashResourceAfterUpdateRemoteState(
    const ClientContext& context,
    const FileOperationCallback& callback,
    const std::string& local_id,
    google_apis::GDataErrorCode status) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (status == google_apis::HTTP_FORBIDDEN) {
    // Editing this entry is not allowed, revert local changes.
    entry_revert_performer_->RevertEntry(local_id, context, callback);
    observer_->OnDriveSyncError(
        file_system::DRIVE_SYNC_ERROR_DELETE_WITHOUT_PERMISSION, local_id);
    return;
  }

  FileError error = GDataToFileError(status);
  if (error == FILE_ERROR_NOT_FOUND) {  // Remove local entry when not found.
    RemoveEntryOnUIThread(blocking_task_runner_.get(), metadata_, local_id,
                          callback);
    return;
  }

  // Now we're done. If the entry is trashed on the server, it'll be also
  // deleted locally on the next update.
  callback.Run(error);
}

void RemovePerformer::UnparentResource(const ClientContext& context,
                                       const FileOperationCallback& callback,
                                       const std::string& resource_id,
                                       const std::string& local_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  scheduler_->GetResourceEntry(
      resource_id,
      context,
      base::Bind(&RemovePerformer::UnparentResourceAfterGetResourceEntry,
                 weak_ptr_factory_.GetWeakPtr(), context, callback, local_id));
}

void RemovePerformer::UnparentResourceAfterGetResourceEntry(
    const ClientContext& context,
    const FileOperationCallback& callback,
    const std::string& local_id,
    google_apis::GDataErrorCode status,
    scoped_ptr<google_apis::ResourceEntry> resource_entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  FileError error = GDataToFileError(status);
  if (error == FILE_ERROR_NOT_FOUND) {  // Remove local entry when not found.
    RemoveEntryOnUIThread(blocking_task_runner_.get(), metadata_, local_id,
                          callback);
    return;
  }

  if (error != FILE_ERROR_OK) {
    callback.Run(error);
    return;
  }

  ResourceEntry entry;
  std::string parent_resource_id;
  if (!ConvertToResourceEntry(*resource_entry, &entry, &parent_resource_id)) {
    callback.Run(FILE_ERROR_NOT_A_FILE);
    return;
  }

  if (!entry.shared_with_me()) {
    // shared_with_me() has changed on the server.
    UnparentResourceAfterUpdateRemoteState(callback, local_id,
                                           google_apis::HTTP_CONFLICT);
    return;
  }

  if (parent_resource_id.empty()) {
    // This entry is unparented already.
    UnparentResourceAfterUpdateRemoteState(callback, local_id,
                                           google_apis::HTTP_NO_CONTENT);
    return;
  }

  scheduler_->RemoveResourceFromDirectory(
      parent_resource_id,
      entry.resource_id(),
      context,
      base::Bind(&RemovePerformer::UnparentResourceAfterUpdateRemoteState,
                 weak_ptr_factory_.GetWeakPtr(), callback, local_id));
}

void RemovePerformer::UnparentResourceAfterUpdateRemoteState(
    const FileOperationCallback& callback,
    const std::string& local_id,
    google_apis::GDataErrorCode status) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  FileError error = GDataToFileError(status);
  if (error != FILE_ERROR_OK) {
    callback.Run(error);
    return;
  }

  base::PostTaskAndReplyWithResult(
      blocking_task_runner_.get(),
      FROM_HERE,
      base::Bind(&UpdateLocalStateAfterUnparent, metadata_, local_id),
      callback);
}

}  // namespace internal
}  // namespace drive
