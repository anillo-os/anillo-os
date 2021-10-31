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
 * Virtual filesystem subsystem.
 */

#ifndef _FERRO_CORE_VFS_H_
#define _FERRO_CORE_VFS_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <ferro/base.h>
#include <ferro/error.h>

FERRO_DECLARATIONS_BEGIN;

/**
 * @addtogroup VFS
 *
 * The virtual filesystem subsystem.
 *
 * @{
 */

FERRO_OPTIONS(uint64_t, fvfs_descriptor_flags) {
	fvfs_descriptor_flag_read     = 1 << 0,
	fvfs_descriptor_flags_write   = 1 << 1,
	fvfs_descriptor_flags_execute = 1 << 2,
};

FERRO_STRUCT_FWD(fvfs_descriptor);

FERRO_STRUCT(fvfs_path_component) {
	const char* component;
	size_t length;

	const char* entire_path;
	size_t entire_path_length;
};

FERRO_STRUCT(fvfs_path) {
	const char* path;
	size_t length;
};

FERRO_ENUM(uint8_t, fvfs_node_type) {
	fvfs_node_type_file,
	fvfs_node_type_directory,
};

FERRO_STRUCT(fvfs_node_info) {
	fvfs_node_type_t type;
};

typedef uint64_t fvfs_list_children_context_t;

/**
 * Initializes the VFS subsystem. Called on kernel startup.
 */
void fvfs_init(void);

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
FERRO_WUR ferr_t fvfs_open_n(const char* path, size_t path_length, fvfs_descriptor_flags_t flags, fvfs_descriptor_t** out_descriptor);

/**
 * Exactly like fvfs_open_n(), but the path length is automatically determined with simple_strlen().
 *
 * @see fvfs_open_n
 */
FERRO_WUR ferr_t fvfs_open(const char* path, fvfs_descriptor_flags_t flags, fvfs_descriptor_t** out_descriptor);

/**
 * Similar to fvfs_open_n(), but the path is allowed to be relative and will be resolved relative to the directory pointed to by @p base_descriptor.
 *
 * @param base_descriptor A descriptor pointing to a VFS directory on which to relatively resolve the given path.
 *
 * Everything else is identical to fvfs_open_n() (with the exception that the path is allowed to be relative).
 *
 * @see fvfs_open_n
 *
 * In addition to the return values that fvfs_open_n() can return, this function also returns addition error codes.
 *
 * @retval ferr_unsupported      The base descriptor's backend does not support relative resolution.
 * @retval ferr_invalid_argument One or more of: 1) @p base_descriptor was `NULL`, or 2) @p base_descriptor does not point to a VFS directory.
 */
FERRO_WUR ferr_t fvfs_open_rn(fvfs_descriptor_t* base_descriptor, const char* path, size_t path_length, fvfs_descriptor_flags_t flags, fvfs_descriptor_t** out_descriptor);

/**
 * Exactly like fvfs_open_rn(), but the path length is automatically determined with simple_strlen().
 *
 * @see fvfs_open_rn
 */
FERRO_WUR ferr_t fvfs_open_r(fvfs_descriptor_t* base_descriptor, const char* path, fvfs_descriptor_flags_t flags, fvfs_descriptor_t** out_descriptor);

/**
 * Tries to retain the given descriptor.
 *
 * @param descriptor The descriptor to try to retain.
 *
 * @retval ferr_ok               The descriptor was successfully retained.
 * @retval ferr_permanent_outage The descriptor was deallocated while this call occurred. It is no longer valid.
 */
FERRO_WUR ferr_t fvfs_retain(fvfs_descriptor_t* descriptor);

/**
 * Releases the given descriptor.
 *
 * @param descriptor The descriptor to release.
 */
void fvfs_release(fvfs_descriptor_t* descriptor);

/**
 * Initializes the given context and begins listing the children of the directory pointed to by the given descriptor.
 *
 * @param descriptor        A descriptor pointing the directory whose children will be listed.
 * @param out_child_array   An array of path descriptors to in which the paths of the children will be placed.
 *                          Whether those paths are absolute or relative to the directory depends on @p absolute.
 *                          It IS valid to pass `NULL` for this argument. In that case, this function (along with fvfs_list_children()) can be used to determine how many children a directory has.
 * @param child_array_count The number of entries for which there is space in the array given in  @p out_children_array.
 *                          If this argument is zero, @p out_child_array is interpreted as being `NULL`. Otherwise, if this argument is non-zero, @p out_child_array MUST NOT be `NULL`.
 * @param absolute          If `true`, the paths returned in the array will be absolute paths. Otherwise, if `false`, the paths returned will only be node names (i.e. paths relative to the directory).
 * @param out_listed_count  A pointer in which to write the number of children read by the call. This will never exceed @p children_array_size.
 * @param out_context       A pointer to the context used between calls to fvfs_list_children().
 *
 * @note It is very important that the same @p out_context and @p out_listed_count arguments are passed to successive calls to fvfs_list_children() and then fvfs_list_children_finish() later.
 *
 * @note See fvfs_list_children() for a note on the ownership of the resources returned.
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
FERRO_WUR ferr_t fvfs_list_children_init(fvfs_descriptor_t* descriptor, fvfs_path_t* out_child_array, size_t child_array_count, bool absolute, size_t* out_listed_count, fvfs_list_children_context_t* out_context);

/**
 * Lists the children of the directory pointed to by the given descriptor.
 *
 * @param descriptor          A descriptor pointing the directory whose children will be listed.
 * @param in_out_child_array  On input, an array of path descriptors populated by a previous call to fvfs_list_children() or fvfs_list_children_init().
 *                            On output, an array of path descriptors in which the paths of the children will be placed.
 *                            Whether those paths are absolute or relative to the directory depends on @p absolute.
 * @param child_array_count   The number of entries for which there is space in the array given in  @p out_child_array.
 * @param absolute            If `true`, the paths returned in the array will be absolute paths. Otherwise, if `false`, the paths returned will only be node names (i.e. paths relative to the directory).
 * @param in_out_listed_count On input, a pointer to the number of items read by the last call to fvfs_list_children_init() or fvfs_list_children(). On output, a pointer in which to write the number of children read by this call. This will never exceed @p child_array_size.
 * @param in_out_context      A pointer to the context used between calls to this function.
 *
 * @note @p out_context and @p out_listed_count MUST be the same ones given to a prior call to fvfs_list_children_init().
 *
 * Here's some sample code:
 * ```c
 * fvfs_list_children_context_t context;
 * size_t count;
 * fvfs_path_t children[32];
 * fvfs_descriptor_t* descriptor = get_a_descriptor_somehow();
 * for (ferr_t status = fvfs_list_children_init(descriptor, children, 32, false, &count, &context); status == ferr_ok; status = fvfs_list_children(descriptor, children, 32, &count, false, &count, &context)) {
 * 	// do something with the children
 * 	// for the example, let's just print their names
 * 	for (size_t i = 0; i < count; ++i) {
 * 		// note: fconsole_logf doesn't support width specification yet
 * 		fconsole_logf("%*s\n", children[i].length, children[i].path);
 * 	}
 * 	fpanic_status(fvfs_list_children_finish(descriptor, children, count, &context));
 * }
 * ```
 *
 * @note The resources allocated by calls to this function are only temporarily owned by the caller
 *       until the next call to fvfs_list_children() or fvfs_list_children_finish().
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
FERRO_WUR ferr_t fvfs_list_children(fvfs_descriptor_t* descriptor, fvfs_path_t* in_out_child_array, size_t child_array_count, bool absolute, size_t* in_out_listed_count, fvfs_list_children_context_t* in_out_context);

/**
 * Disposes of the resources held by an fvfs_list_children() context and array.
 *
 * @param descriptor     A descriptor pointing the directory whose children have been listed.
 * @param child_array    An array of path descriptors in which the paths of the children have been placed.
 * @param listed_count   The number of items read by the last call to fvfs_list_children_init() or fvfs_list_children().
 * @param in_out_context A pointer to the context used between calls to fvfs_list_children().
 *
 * @note This function must ALWAYS be called after the caller is done listing a directory's children.
 *       For example, if you are calling fvfs_list_children() in a loop, you must call this function after the loop has fully completed.
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
FERRO_WUR ferr_t fvfs_list_children_finish(fvfs_descriptor_t* descriptor, fvfs_path_t* child_array, size_t listed_count, fvfs_list_children_context_t* in_out_context);

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
FERRO_WUR ferr_t fvfs_copy_path(fvfs_descriptor_t* descriptor, bool absolute, char* out_path_buffer, size_t path_buffer_size, size_t* out_length);

/**
 * Copies the information for the node pointed to by the given descriptor into the given pointer.
 *
 * @param descriptor A descriptor pointing to the node whose information shall be copied.
 * @param out_info   A pointer to an ::fvfs_node_type structure into which the node's information will be copied.
 *
 * @retval ferr_ok               The information was successfully copied into @p out_info.
 * @retval ferr_invalid_argument One or more of: 1) @p descriptor was `NULL`, 2) @p out_info was `NULL`.
 * @retval ferr_unsupported      Copying the information of the node pointed to by the given descriptor was not supported by the descriptor's backend.
 * @retval ferr_forbidden        Copying the information of the node was not allowed.
 */
FERRO_WUR ferr_t fvfs_copy_info(fvfs_descriptor_t* descriptor, fvfs_node_info_t* out_info);

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
FERRO_WUR ferr_t fvfs_read(fvfs_descriptor_t* descriptor, size_t offset, void* buffer, size_t buffer_size, size_t* out_read_count);

FERRO_WUR ferr_t fvfs_write(fvfs_descriptor_t* descriptor, size_t offset, const void* buffer, size_t buffer_size, size_t* out_written_count);

/**
 * Determines whether the given path is absolute (i.e. whether it starts from the root of the filesystem).
 *
 * @param path        The UTF-8 string of slash-separated path components representing a node's path.
 * @param path_length The length of the string given in @p path.
 *
 * @returns `true` if the path is absolute, `false` otherwise (including if @p path was `NULL`).
 */
bool fvfs_path_is_absolute_n(const char* path, size_t path_length);

/**
 * Exactly like fvfs_path_is_absolute_n(), but the path length is automatically determined with simple_strlen().
 *
 * @see fvfs_path_is_absolute_n
 */
bool fvfs_path_is_absolute(const char* path);

/**
 * Initializes a path component iterator with the given context.
 *
 * The iterator is set up to point to the first component (if any).
 *
 * @param               path The UTF-8 string of slash-separated path components representing a node's path.
 * @param        path_length The length of the string given in @p path.
 * @param[out] out_component The path component iterator to initialize.
 *
 * @note @p path MUST remain valid for as long as the iterator is used.
 *
 * @retval ferr_ok               The path component iterator has been successfully initialized with the first component in the given path.
 * @retval ferr_permanent_outage There were no more path components in the given path; the iterator is left unmodified. This can only happen with an empty path.
 * @retval ferr_invalid_argument One or more of: 1) @p path was `NULL`, or 2) @p out_component was `NULL`.
 */
FERRO_WUR ferr_t fvfs_path_component_first_n(const char* path, size_t path_length, fvfs_path_component_t* out_component);

/**
 * Exactly like fvfs_path_component_first_n(), but the path length is automatically determined with simple_strlen().
 *
 * @see fvfs_path_component_first_n
 */
FERRO_WUR ferr_t fvfs_path_component_first(const char* path, fvfs_path_component_t* out_component);

/**
 * Advances the given path component iterator to the next path component.
 *
 * @param[in, out] in_out_component The path component iterator to advance.
 *
 * @retval ferr_ok               The path component iterator has been successfully advanced to the next component in the path.
 * @retval ferr_permanent_outage There were no more path components following the current path component; the iterator is left unmodified.
 * @retval ferr_invalid_argument @p in_out_component was `NULL`.
 */
FERRO_WUR ferr_t fvfs_path_component_next(fvfs_path_component_t* in_out_component);

/**
 * @}
 */

FERRO_DECLARATIONS_END;

#endif // _FERRO_CORE_VFS_H_
