#include <libsys/streams.h>
#include <libsys/objects.private.h>
#include <gen/libsyscall/syscall-wrappers.h>
#include <libsys/abort.h>

LIBSYS_STRUCT(sys_stream_object) {
	sys_object_t object;
	sys_stream_handle_t handle;
};

static void sys_stream_destroy(sys_object_t* object);

static const sys_object_class_t stream_class = {
	LIBSYS_OBJECT_CLASS_INTERFACE(NULL),
	.destroy = sys_stream_destroy,
};

LIBSYS_OBJECT_CLASS_GETTER(stream, stream_class);

static void sys_stream_destroy(sys_object_t* object) {
	sys_stream_object_t* stream = (void*)object;

	if (stream->handle != SYS_STREAM_HANDLE_INVALID) {
		sys_abort_status(libsyscall_wrapper_fd_close(stream->handle));
	}

	sys_object_destroy(object);
};

ferr_t sys_stream_open_special(sys_stream_special_id_t special_id, sys_stream_t** out_stream) {
	ferr_t status = ferr_ok;
	sys_stream_t* xstream = NULL;
	sys_stream_object_t* stream = NULL;

	// allocate the object before trying to open a handle
	// this is done in this order because memory pool allocation/freeing is cheaper than FD creation/destruction
	status = sys_object_new(&stream_class, sizeof(sys_stream_object_t) - sizeof(sys_object_t), &xstream);
	if (status != ferr_ok) {
		goto out;
	}
	stream = (void*)xstream;

	stream->handle = SYS_STREAM_HANDLE_INVALID;

	status = sys_stream_open_special_handle(special_id, &stream->handle);
	if (status != ferr_ok) {
		goto out;
	}

out:
	if (status == ferr_ok) {
		*out_stream = xstream;
	} else {
		if (xstream) {
			sys_release(xstream);
		}
	}
	return status;
};

ferr_t sys_stream_open_special_handle(sys_stream_special_id_t special_id, sys_stream_handle_t* out_stream_handle) {
	ferr_t status = ferr_ok;

	switch (special_id) {
		case sys_stream_special_id_console_standard_output:
			status = libsyscall_wrapper_fd_open_special(1, out_stream_handle);
			break;
		default:
			status = ferr_invalid_argument;
			break;
	}

out:
	return status;
};

ferr_t sys_stream_close_handle(sys_stream_handle_t stream_handle) {
	return libsyscall_wrapper_fd_close(stream_handle);
};

ferr_t sys_stream_handle(sys_stream_t* xstream, sys_stream_handle_t* out_stream_handle) {
	sys_stream_object_t* stream = (void*)xstream;
	ferr_t status = ferr_ok;

	if (!stream) {
		status = ferr_invalid_argument;
		goto out;
	}

	if (out_stream_handle) {
		*out_stream_handle = stream->handle;
	}

out:
	return status;
};

ferr_t sys_stream_read(sys_stream_t* xstream, size_t buffer_length, void* out_buffer, size_t* out_read_count) {
	ferr_t status = ferr_ok;
	sys_stream_handle_t stream_handle = SYS_STREAM_HANDLE_INVALID;

	status = sys_stream_handle(xstream, &stream_handle);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_stream_read_handle(stream_handle, buffer_length, out_buffer, out_read_count);

out:
	return status;
};

ferr_t sys_stream_read_handle(sys_stream_handle_t stream_handle, size_t buffer_length, void* out_buffer, size_t* out_read_count) {
	uint64_t read_count = 0;
	ferr_t status = libsyscall_wrapper_fd_read(stream_handle, 0, buffer_length, out_buffer, &read_count);
	if (out_read_count) {
		*out_read_count = read_count;
	}
	return status;
};

ferr_t sys_stream_write(sys_stream_t* xstream, size_t buffer_length, const void* buffer, size_t* out_written_count) {
	ferr_t status = ferr_ok;
	sys_stream_handle_t stream_handle = SYS_STREAM_HANDLE_INVALID;

	status = sys_stream_handle(xstream, &stream_handle);
	if (status != ferr_ok) {
		goto out;
	}

	status = sys_stream_write_handle(stream_handle, buffer_length, buffer, out_written_count);

out:
	return status;
};

ferr_t sys_stream_write_handle(sys_stream_handle_t stream_handle, size_t buffer_length, const void* buffer, size_t* out_written_count) {
	uint64_t written_count = 0;
	ferr_t status = libsyscall_wrapper_fd_write(stream_handle, 0, buffer_length, buffer, &written_count);
	if (out_written_count) {
		*out_written_count = written_count;
	}
	return status;
};
