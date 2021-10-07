import os
import errno
import subprocess

# from https://stackoverflow.com/a/600612/6620880
def mkdir_p(path):
	try:
		os.makedirs(path)
	except OSError as exc:
		if not (exc.errno == errno.EEXIST and os.path.isdir(path)):
			raise

# adapted from https://stackoverflow.com/a/53808695/6620880
def to_padded_hex(value, hex_digits=2):
	return '0x{0:0{1}x}'.format(value if isinstance(value, int) else ord(value), hex_digits)

def to_c_array(array_name, values, array_type='uint8_t', formatter=to_padded_hex, column_count=8, static=True):
	if len(values) == 0:
		return '{}{} {}[] = {{}};\n'.format('static ' if static else '', array_type, array_name)
	values = [formatter(v) for v in values]
	rows = [values[i:i + column_count] for i in range(0, len(values), column_count)]
	body = ',\n\t'.join([', '.join(r) for r in rows])
	return '{}{} {}[] = {{\n\t{},\n}};\n'.format('static ' if static else '', array_type, array_name, body)

def run_or_fail(command, print_on_fail=True):
	output = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

	if output.returncode != 0:
		print(output.stdout.decode())
		raise subprocess.CalledProcessError(output.returncode, command, output.stdout)

def round_up_to_multiple(number, multiple):
	return multiple * int((number + (multiple - 1)) / multiple)

def round_down_to_multiple(number, multiple):
	return multiple * int(number / multiple)
