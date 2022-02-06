/*
 * This file is part of Anillo OS
 * Copyright (C) 2021 Anillo OS Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Virtual filesystem subsystem, backend API.
 */

#ifndef _FERRO_CORE_VFS_BACKEND_H_
#define _FERRO_CORE_VFS_BACKEND_H_

#include <stddef.h>
#include <stdint.h>

#include <ferro/base.h>
#include <ferro/error.h>
#include <ferro/core/vfs.h>
#include <ferro/core/refcount.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup VFS
 *
 * @{
 */

FERRO_STRUCT_FWD(fvfs_backend);
FERRO_STRUCT_FWD(fvfs_mount);

FERRO_OPTIONS(uint64_t, fvfs_descriptor_private_flags) {
	/**
	 * This flag tells the VFS subsystem that fvfs_descriptor_destroy() should free the descriptor using the mempool subsystem.
	 */
	fvfs_descriptor_private_flag_mempool_free_on_destroy = 1ULL << 32,
};

FERRO_STRUCT(fvfs_descriptor) {
	frefcount_t reference_count;
	fvfs_descriptor_flags_t flags;
	fvfs_mount_t* mount;
};

/**
 * Opens a new descriptor for the given path with the given flags.
 *
 * This callback is only allowed to return the same errors that fvfs_open() can return (`ferr_unknown` is permitted of course, like it is for all other functions).
 *
 * The backend needs to allocate the ::fvfs_descriptor at a minimum, but backends will typically also allocate their own extra information to add after the descriptor structure.
 *
 * The backend needs to call fvfs_descriptor_init() to initialize the descriptor at a minimum; for backends that allocate their own information, this is the place to initialize it.
 *
 * The path component array passed to this callback will remain valid for as long as the descriptor does (upon successful return).
 */
typedef ferr_t (*fvfs_backend_open_f)(void* context, fvfs_mount_t* mount, const char* path, size_t path_length, fvfs_descriptor_flags_t flags, fvfs_descriptor_t** out_descriptor);

/**
 * Closes the given descriptor.
 *
 * This should take care of any cleanup the backend needs to do, then call fvfs_descriptor_destroy(), and finally, free the memory allocated for the descriptor.
 */
typedef ferr_t (*fvfs_backend_close_f)(void* context, fvfs_descriptor_t* descriptor);

/**
 * Begins listing children of a directory.
 *
 * This callback is only allowed to return the same errors that fvfs_list_children_init() can return (`ferr_unknown` is permitted of course, like it is for all other functions).
 *
 * The caller context will be the same between successive calls for the same listing and can be interpreted and assigned by the backend however it likes.
 *
 * Almost all of the preconditions will be verified before calling this callback.
 * The only one that must be verified by the callback itself is whether the descriptor refers to a directory.
 */
typedef ferr_t (*fvfs_backend_list_children_init_f)(void* context, fvfs_descriptor_t* descriptor, fvfs_path_t* out_child_array, size_t child_array_count, bool absolute, size_t* out_listed_count, fvfs_list_children_context_t* out_context);

/**
 * Continues listing the children of a directory.
 *
 * This callback is only allowed to return the same errors that fvfs_list_children() can return (`ferr_unknown` is permitted of course, like it is for all other functions).
 *
 * Almost all of the preconditions will be verified before calling this callback.
 * The only one that must be verified by the callback itself is whether the descriptor refers to a directory.
 */
typedef ferr_t (*fvfs_backend_list_children_f)(void* context, fvfs_descriptor_t* descriptor, fvfs_path_t* in_out_child_array, size_t child_array_count, bool absolute, size_t* in_out_listed_count, fvfs_list_children_context_t* in_out_context);

/**
 * Cleans up the resources held by a listing.
 *
 * This callback is only allowed to return the same errors that fvfs_list_children_finish() can return (`ferr_unknown` is permitted of course, like it is for all other functions).
 *
 * Almost all of the preconditions will be verified before calling this callback.
 * The only one that must be verified by the callback itself is whether the descriptor refers to a directory.
 */
typedef ferr_t (*fvfs_backend_list_children_finish_f)(void* context, fvfs_descriptor_t* descriptor, fvfs_path_t* child_array, size_t listed_count, fvfs_list_children_context_t* in_out_context);

/**
 * Copies the mount-absolute path of the node pointed to by the given descriptor.
 *
 * This callback is only allowed to return the same errors that fvfs_copy_path() can return (`ferr_unknown` is permitted of course, like it is for all other functions).
 *
 * The invalid argument preconditions have been verified before calling this callback.
 *
 * @note While you are allowed to leave this unimplemented, this function is used to implement relative descriptor creation.
 *       Therefore, if you don't implement this function, descriptors created by your backend cannot be used as base descriptors for relative descriptor creation.
 *
 * @note The mount-absolute path is NOT the same as the absolute path.
 *       As the name implies, the mount-absolute path is the absolute path *taking the mount point to be the root*.
 *
 * @note The copied path MUST begin with a slash.
 */
typedef ferr_t (*fvfs_backend_copy_path_f)(void* context, fvfs_descriptor_t* descriptor, bool absolute, char* out_path_buffer, size_t path_buffer_size, size_t* out_length);

/**
 * Copies the information for the node pointed to by the given descriptor.
 *
 * This callback is only allowed to return the same errors that fvfs_copy_info() can return (`ferr_unknown` is permitted of course, like it is for all other functions)
 *
 * The invalid argument preconditions have been verified before calling this callback.
 *
 * @note While you are allowed to leave this unimplemented, this function is used to implement relative descriptor creation.
 *       Therefore, if you don't implement this function, descriptors created by your backend cannot be used as base descriptors for relative descriptor creation.
 */
typedef ferr_t (*fvfs_backend_copy_info_f)(void* context, fvfs_descriptor_t* descriptor, fvfs_node_info_t* out_info);

typedef ferr_t (*fvfs_backend_read_f)(void* context, fvfs_descriptor_t* descriptor, size_t offset, void* buffer, size_t buffer_size, size_t* out_read_count);

typedef ferr_t (*fvfs_backend_write_f)(void* context, fvfs_descriptor_t* descriptor, size_t offset, const void* buffer, size_t buffer_size, size_t* out_written_count);

/**
 * A structure that contains all the necessary information to describe a VFS backend.
 *
 * A VFS backend is used to manage a VFS subtree. Each backend contains a set of callbacks used to perform operations within that subtree.
 *
 * For a VFS backend to be used for a particular subtree, it must mounted on that subtree. Each mount can have its own backend-specific context data.
 * This data is assigned when the mount is created and is passed to the backend callbacks whenever they are called on that particular mount.
 *
 * For a mount-based backend, the only required methods are #open and #close. All others can be `NULL` pointers.
 * For an anonymous descriptor backend, there are no required methods.
 */
FERRO_STRUCT(fvfs_backend) {
	fvfs_backend_open_f open;
	fvfs_backend_close_f close;
	fvfs_backend_list_children_init_f list_children_init;
	fvfs_backend_list_children_f list_children;
	fvfs_backend_list_children_finish_f list_children_finish;
	fvfs_backend_copy_path_f copy_path;
	fvfs_backend_copy_info_f copy_info;
	fvfs_backend_read_f read;
	fvfs_backend_write_f write;
};

FERRO_WUR ferr_t fvfs_descriptor_init(fvfs_descriptor_t* descriptor, fvfs_mount_t* mount, fvfs_descriptor_flags_t flags);
void fvfs_descriptor_destroy(fvfs_descriptor_t* descriptor);

/**
 * Mounts a backend on a subtree.
 *
 * @param           path The UTF-8 string representing the path to mount it on.
 * @param    path_length The length of the string given in @p path.
 * @param        backend A structure describing the backend to mount.
 * @param        context Optional backend-specific context argument to pass to backend callbacks when calling them.
 *
 * @retval ferr_ok                  The mount was successfully created.
 * @retval ferr_invalid_argument    One or more of: 1) @p path was `NULL`, 2) @p backend was `NULL`.
 * @retval ferr_temporary_outage    There were insufficient resources available to create the mount.
 * @retval ferr_already_in_progress The given mountpoint was not empty.
 * @retval ferr_forbidden           The caller was not allowed to create a mount at the given path.
 *
 * @note @p backend must remain valid for as long as the mount is active.
 */
FERRO_WUR ferr_t fvfs_mount(const char* path, size_t path_length, const fvfs_backend_t* backend, void* context);

/**
 * Unmounts the backend at the given subtree.
 *
 * @param           path The UTF-8 string representing the path to unmount.
 * @param    path_length The length of the string given in @p path.
 *
 * @retval ferr_ok                  The mount was successfully destroyed.
 * @retval ferr_invalid_argument    @p path was `NULL`.
 * @retval ferr_already_in_progress The mount is in-use and cannot be unmounted until pending operations have completed and open descriptors have been released.
 * @retval ferr_forbidden           The caller was not allowed to remove a mount at the given path.
 * @retval ferr_no_such_resource    There was no mount at the given path.
 */
FERRO_WUR ferr_t fvfs_unmount(const char* path, size_t path_length);

/**
 * Opens an anonymous VFS descriptor.
 *
 * These types of descriptors can be used to operate on any kind of data using a file-like interface.
 *
 * Typically, these kinds of descriptors are treated like files. However, if the `open` function is implemented in the given backend,
 * then the created node can be used like a directory, opening descriptors for and operating on its "children".
 *
 * @note @p backend is copied into some internal structure; therefore, it need not remain valid past the call.
 *       This also means that, once an anonymous descriptor is created, its backend functions cannot be changed.
 *
 * @note Once the descriptor's last reference is released, the `close` method, if it is present, will be called before freeing the descriptor's resources.
 *       However, **there is no way to stop the descriptor's resources (i.e. memory) from being released**.
 */
FERRO_WUR ferr_t fvfs_open_anonymous(const char* name, size_t name_length, const fvfs_backend_t* backend, void* context, fvfs_descriptor_t** out_descriptor);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_VFS_BACKEND_H_
