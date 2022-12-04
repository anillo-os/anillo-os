/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
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

#ifndef _VFSMAN_VFS_H_
#define _VFSMAN_VFS_H_

#include <libvfs/base.h>
#include <vfsman/objects.h>

LIBVFS_DECLARATIONS_BEGIN;

FERRO_OPTIONS(uint64_t, vfsman_descriptor_flags) {
	vfsman_descriptor_flag_read     = 1 << 0,
	vfsman_descriptor_flags_write   = 1 << 1,
	vfsman_descriptor_flags_execute = 1 << 2,
};

VFSMAN_OBJECT_CLASS(descriptor);

FERRO_ENUM(uint8_t, vfsman_node_type) {
	vfsman_node_type_file,
	vfsman_node_type_directory,
};

FERRO_STRUCT(vfsman_node_info) {
	vfsman_node_type_t type;
};

typedef uint64_t vfsman_list_children_context_t;

void vfsman_init(void);

/**
 * Creates a descriptor with the given flags for the VFS node at the given path.
 *
 * @param                path The UTF-8 string of slash-separated path components representing the node's absolute path.
 * @param         path_length The length of the string given in @p path.
 * @param               flags Flags to set on the descriptor when creating it.
 * @param[out] out_descriptor A pointer in which to write the resulting descriptor upon success.
 *                            If this is `NULL`, no node descriptor is actually created; this can be used to test for the existence of a node.
 *
 * @note The caller is granted a single reference on the newly created descriptor.
 *
 * @retval ferr_ok               The descriptor was successfully created.
 * @retval ferr_temporary_outage There were insufficient resources to create the descriptor.
 * @retval ferr_invalid_argument One or more of: 1) @p path was `NULL`, 2) @p flags contained one or more invalid flags, or 3) @p path was not an absolute path.
 * @retval ferr_no_such_resource There was no VFS node at the given path.
 * @retval ferr_forbidden        Access to the given node was not allowed (possibly due to forbidden descriptor flags).
 */
LIBVFS_WUR ferr_t vfsman_open_n(const char* path, size_t path_length, vfsman_descriptor_flags_t flags, vfsman_descriptor_t** out_descriptor);

/**
 * Exactly like vfsman_open_n(), but the path length is automatically determined with simple_strlen().
 *
 * @see vfsman_open_n
 */
LIBVFS_WUR ferr_t vfsman_open(const char* path, vfsman_descriptor_flags_t flags, vfsman_descriptor_t** out_descriptor);

/**
 * Similar to vfsman_open_n(), but the path is allowed to be relative and will be resolved relative to the directory pointed to by @p base_descriptor.
 *
 * @param base_descriptor A descriptor pointing to a VFS directory on which to relatively resolve the given path.
 *
 * Everything else is identical to vfsman_open_n() (with the exception that the path is allowed to be relative).
 *
 * @see vfsman_open_n
 *
 * In addition to the return values that vfsman_open_n() can return, this function also returns addition error codes.
 *
 * @retval ferr_unsupported      The base descriptor's backend does not support relative resolution.
 * @retval ferr_invalid_argument One or more of: 1) @p base_descriptor was `NULL`, or 2) @p base_descriptor does not point to a VFS directory.
 */
LIBVFS_WUR ferr_t vfsman_open_rn(vfsman_descriptor_t* base_descriptor, const char* path, size_t path_length, vfsman_descriptor_flags_t flags, vfsman_descriptor_t** out_descriptor);

/**
 * Exactly like vfsman_open_rn(), but the path length is automatically determined with simple_strlen().
 *
 * @see vfsman_open_rn
 */
LIBVFS_WUR ferr_t vfsman_open_r(vfsman_descriptor_t* base_descriptor, const char* path, vfsman_descriptor_flags_t flags, vfsman_descriptor_t** out_descriptor);

/**
 * Initializes the given context and begins listing the children of the directory pointed to by the given descriptor.
 *
 * @param descriptor        A descriptor pointing the directory whose children will be listed.
 * @param out_child_array   An array of path descriptors to in which the paths of the children will be placed.
 *                          Whether those paths are absolute or relative to the directory depends on @p absolute.
 *                          It IS valid to pass `NULL` for this argument. In that case, this function (along with vfsman_list_children()) can be used to determine how many children a directory has.
 * @param child_array_count The number of entries for which there is space in the array given in  @p out_children_array.
 *                          If this argument is zero, @p out_child_array is interpreted as being `NULL`. Otherwise, if this argument is non-zero, @p out_child_array MUST NOT be `NULL`.
 * @param absolute          If `true`, the paths returned in the array will be absolute paths. Otherwise, if `false`, the paths returned will only be node names (i.e. paths relative to the directory).
 * @param out_listed_count  A pointer in which to write the number of children read by the call. This will never exceed @p children_array_size.
 * @param out_context       A pointer to the context used between calls to vfsman_list_children().
 *
 * @note It is very important that the same @p out_context and @p out_listed_count arguments are passed to successive calls to vfsman_list_children() and then vfsman_list_children_finish() later.
 *
 * @note See vfsman_list_children() for a note on the ownership of the resources returned.
 *
 * @retval ferr_ok               The context and listed-count have been successfully initialized and the first batch of children have been placed into the child array.
 * @retval ferr_temporary_outage There were insufficient resources to initialize the context and populate the child array.
 *                               When this code is returned, it is safe to immediately retry the call.
 * @retval ferr_permanent_outage There are no more children to list.
 * @retval ferr_invalid_argument One or more of:
 *                               1) @p descriptor was `NULL`,
 *                               2) @p descriptor points to a VFS node that is not a directory,
 *                               3) @p out_child_array was `NULL` and @p child_array_count was non-zero,
 *                               4) @p out_listed_count was `NULL`, or
 *                               5) @p out_context was `NULL`.
 * @retval ferr_forbidden        Listing the children of the given directory was not allowed.
 * @retval ferr_unsupported      Listing the children of the given directory was not supported by the descriptor's backend.
 */
LIBVFS_WUR ferr_t vfsman_list_children_init(vfsman_descriptor_t* descriptor, sys_path_t* out_child_array, size_t child_array_count, bool absolute, size_t* out_listed_count, vfsman_list_children_context_t* out_context);

/**
 * Lists the children of the directory pointed to by the given descriptor.
 *
 * @param descriptor          A descriptor pointing the directory whose children will be listed.
 * @param in_out_child_array  On input, an array of path descriptors populated by a previous call to vfsman_list_children() or vfsman_list_children_init().
 *                            On output, an array of path descriptors in which the paths of the children will be placed.
 *                            Whether those paths are absolute or relative to the directory depends on @p absolute.
 * @param child_array_count   The number of entries for which there is space in the array given in  @p out_child_array.
 * @param absolute            If `true`, the paths returned in the array will be absolute paths. Otherwise, if `false`, the paths returned will only be node names (i.e. paths relative to the directory).
 * @param in_out_listed_count On input, a pointer to the number of items read by the last call to vfsman_list_children_init() or vfsman_list_children(). On output, a pointer in which to write the number of children read by this call. This will never exceed @p child_array_size.
 * @param in_out_context      A pointer to the context used between calls to this function.
 *
 * @note @p out_context and @p out_listed_count MUST be the same ones given to a prior call to vfsman_list_children_init().
 *
 * Here's some sample code:
 * ```c
 * vfsman_list_children_context_t context;
 * size_t count;
 * sys_path_t children[32];
 * vfsman_descriptor_t* descriptor = get_a_descriptor_somehow();
 * for (ferr_t status = vfsman_list_children_init(descriptor, children, 32, false, &count, &context); status == ferr_ok; status = vfsman_list_children(descriptor, children, 32, &count, false, &count, &context)) {
 * 	// do something with the children
 * 	// for the example, let's just print their names
 * 	for (size_t i = 0; i < count; ++i) {
 * 		sys_console_log_f("%.*s\n", children[i].length, children[i].contents);
 * 	}
 * }
 * sys_abort_status_log(vfsman_list_children_finish(descriptor, children, count, &context));
 * ```
 *
 * @note The resources allocated by calls to this function are only temporarily owned by the caller
 *       until the next call to vfsman_list_children() or vfsman_list_children_finish().
 *       For longer ownership, the caller should copy the data themselves.
 *
 * @retval ferr_ok               A set of children have been placed into the child array and the listed-count and context have been updated.
 * @retval ferr_temporary_outage There were insufficient resources to populate the child array.
 *                               When this code is returned, it is safe to immediately retry the call.
 * @retval ferr_permanent_outage There are no more children to list.
 * @retval ferr_invalid_argument One or more of:
 *                               1) @p descriptor was `NULL`,
 *                               2) @p descriptor points to a VFS node that is not a directory,
 *                               3) @p in_out_child_array was `NULL` and @p child_array_count was non-zero,
 *                               4) @p in_out_listed_count was `NULL`, or
 *                               5) @p in_out_context was `NULL`.
 * @retval ferr_forbidden        Listing the children of the given directory was not allowed.
 * @retval ferr_unsupported      Listing the children of the given directory was not supported by the descriptor's backend.
 */
LIBVFS_WUR ferr_t vfsman_list_children(vfsman_descriptor_t* descriptor, sys_path_t* in_out_child_array, size_t child_array_count, bool absolute, size_t* in_out_listed_count, vfsman_list_children_context_t* in_out_context);

/**
 * Disposes of the resources held by an vfsman_list_children() context and array.
 *
 * @param descriptor     A descriptor pointing the directory whose children have been listed.
 * @param child_array    An array of path descriptors in which the paths of the children have been placed.
 * @param listed_count   The number of items read by the last call to vfsman_list_children_init() or vfsman_list_children().
 * @param in_out_context A pointer to the context used between calls to vfsman_list_children().
 *
 * @note This function must ALWAYS be called after the caller is done listing a directory's children.
 *       For example, if you are calling vfsman_list_children() in a loop, you must call this function after the loop has fully completed.
 *
 * @note It IS valid to finish listing a directory's children early (i.e. to not list them all). All you must do is ensure you call this function when you decide to stop.
 *
 * @retval ferr_ok               The resources were successfully disposed.
 * @retval ferr_temporary_outage Disposal of the resources failed temporarily.
 * @retval ferr_invalid_argument One or more of:
 *                               1) @p descriptor was `NULL`,
 *                               2) @p descriptor points to a VFS node that is not a directory,
 *                               3) @p child_array was `NULL` and @p listed_count was non-zero,
 *                               4) @p in_out_context was `NULL`.
 * @retval ferr_unsupported      Listing the children of the given directory was not supported by the descriptor's backend.
 */
LIBVFS_WUR ferr_t vfsman_list_children_finish(vfsman_descriptor_t* descriptor, sys_path_t* child_array, size_t listed_count, vfsman_list_children_context_t* in_out_context);

/**
 * Copies the path of the node pointed to by the given descriptor into the given buffer.
 *
 * @param descriptor       A descriptor pointing to the node whose path shall be copied.
 * @param absolute         If `true`, the absolute path of the node will be copied; otherwise, if `false`, only the node's name will be copied.
 * @param out_path_buffer  The buffer in which to store the result.
 * @param path_buffer_size The size of the path buffer, in bytes.
 * @param out_length       A pointer in which the number of bytes the node name occupies shall be written.
 *
 * @note If there is enough space to store the result, it will be stored. Otherwise, nothing will be written and the required length will be written to @p out_length.
 *       The length never includes the null terminator, as this is only added if the buffer is long enough to store the result AND a null terminator.
 *       If a null terminator cannot be written, success is still returned.
 *
 * @note Setting @p out_path_buffer to `NULL` and @p path_buffer_size to 0 can be used to determine the length of the path.
 *
 * @retval ferr_ok               @p out_path_buffer was large enough to store the result and the node name has been copied into it. @p out_length holds the length of the node name (not including the null terminator).
 * @retval ferr_too_big          @p out_path_buffer was too small to store the result and required length has been written to @p out_length.
 * @retval ferr_invalid_argument One or more of 1) @p descriptor was `NULL`, or 2) @p out_path_buffer was `NULL` but @p path_buffer_size was not `0`.
 * @retval ferr_unsupported      Copying the path of the node pointed to by the given descriptor was not supported by the descriptor's backend.
 * @retval ferr_forbidden        Copying the path of the node was not allowed.
 */
LIBVFS_WUR ferr_t vfsman_copy_path(vfsman_descriptor_t* descriptor, bool absolute, char* out_path_buffer, size_t path_buffer_size, size_t* out_length);

/**
 * Copies the information for the node pointed to by the given descriptor into the given pointer.
 *
 * @param descriptor A descriptor pointing to the node whose information shall be copied.
 * @param out_info   A pointer to an ::vfsman_node_info structure into which the node's information will be copied.
 *
 * @retval ferr_ok               The information was successfully copied into @p out_info.
 * @retval ferr_invalid_argument One or more of: 1) @p descriptor was `NULL`, 2) @p out_info was `NULL`.
 * @retval ferr_unsupported      Copying the information of the node pointed to by the given descriptor was not supported by the descriptor's backend.
 * @retval ferr_forbidden        Copying the information of the node was not allowed.
 */
LIBVFS_WUR ferr_t vfsman_copy_info(vfsman_descriptor_t* descriptor, vfsman_node_info_t* out_info);

/**
 * Reads some data from the node pointed to by the given descriptor.
 *
 * @param descriptor     A descriptor pointing to a VFS node whose contents shall be read.
 * @param offset         An offset from which to start reading from the file contents.
 * @param buffer         A buffer into which the data will be read.
 * @param buffer_size    The size of @p buffer.
 * @param out_read_count A pointer in which the count of how much data was read will be written.
 *
 * @note The parameter descriptions for this function are purposefully vague because exactly what a read operation does depends on the node in question and its backend.
 *       However, the most common definition is that reading is only valid for file nodes and that all quantities are in bytes; reading will read a number of bytes from
 *       the file's contents into the given buffer.
 *
 * @note In some cases (depending on the node and its backend), it is valid to pass `NULL` and `0` for @p buffer and @p buffer_size (respectively), in which case, the amount
 *       of data that can be read will by returned in @p out_read_count.
 *
 * @retval ferr_ok               Data has been successfully read into @p buffer.
 * @retval ferr_invalid_argument One or more of: 1) @p descriptor was `NULL`. However, this error may be returned in more cases depending on the node and its backend.
 * @retval ferr_permanent_outage No data was read and no more data can be read.
 * @retval ferr_unsupported      The read operation is unsupported for the given node and its backend.
 * @retval ferr_temporary_outage Data could not be read from the node.
 *                               When this code is returned, it is safe to immediately retry the call.
 * @retval ferr_forbidden        Reading from the node was not allowed.
 */
LIBVFS_WUR ferr_t vfsman_read(vfsman_descriptor_t* descriptor, size_t offset, void* buffer, size_t buffer_size, size_t* out_read_count);

LIBVFS_WUR ferr_t vfsman_write(vfsman_descriptor_t* descriptor, size_t offset, const void* buffer, size_t buffer_size, size_t* out_written_count);

LIBVFS_DECLARATIONS_END;

#endif // _VFSMAN_VFS_H_
