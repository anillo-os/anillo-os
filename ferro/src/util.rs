pub const fn decode_utf8_and_length(bytes: &[u8]) -> Result<(char, u8), ()> {
	if bytes.len() == 0 {
		return Err(())
	}

	let first_byte = bytes[0];
	let mut required_length = 1;
	let mut utf32_result: u32 = 0;

	if first_byte & 0x80 {
		if (first_byte & 0x20) == 0 {
			required_length = 2;
			utf32_result = (first_byte & 0x1f) as u32;
		} else if (first_byte & 0x10) == 0 {
			required_length = 3;
			utf32_result = (first_byte & 0x0f) as u32;
		} else if (first_byte & 0x08) == 0 {
			required_length = 4;
			utf32_result = (first_byte & 0x07) as u32;
		} else {
			return Err(())
		}
	} else {
		utf32_result = first_byte;
	}

	if bytes.len() < required_length {
		return Err(())
	}

	for i in 1..required_length {
		utf32_result <<= 6;
		utf32_result |= (bytes[i] & 0x3f) as u32;
	}

	Ok((char::from_u32(utf32_result).unwrap(), required_length))
}

pub const fn decode_utf8(bytes: &[u8]) -> Result<char, ()> {
	decode_utf8_and_length(bytes).map(|x| x.0)
}
