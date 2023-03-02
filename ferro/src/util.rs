pub const fn decode_utf8_and_length(bytes: &[u8]) -> Option<(char, u8)> {
	if bytes.len() == 0 {
		return None
	}

	let first_byte = bytes[0];
	let mut required_length: u8 = 1;
	let mut utf32_result: u32;

	if (first_byte & 0x80) != 0 {
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
			return None
		}
	} else {
		utf32_result = first_byte as u32;
	}

	if bytes.len() < required_length as usize {
		return None
	}

	let mut i: u8 = 1;
	while i < required_length {
		utf32_result <<= 6;
		utf32_result |= (bytes[i as usize] & 0x3f) as u32;
		i += 1;
	}

	Some((char::from_u32(utf32_result).unwrap(), required_length))
}

pub const fn decode_utf8(bytes: &[u8]) -> Option<char> {
	match decode_utf8_and_length(bytes) {
		Some(x) => Some(x.0),
		None => None,
	}
}

pub const fn slices_are_equal<T: ~const PartialEq>(a: &[T], b: &[T]) -> bool {
	if a.len() != b.len() {
		return false;
	}
	let mut i: usize = 0;
	while i < a.len() {
		if a[i] != b[i] {
			return false
		}
		i += 1;
	}
	return true
}

#[const_trait]
pub trait ConstSlice<T> {
	fn const_copy_from_slice(&mut self, source: &Self) where T: Copy;
	fn const_unroll<const N: usize>(&self) -> [T; N] where T: Copy + ~const Default;
}

#[const_trait]
pub trait ConstSizedSlice<T, const N: usize> {
	fn const_concat<const M: usize>(&self, other: &[T; M]) -> [T; N + M] where T: Copy + ~const Default;
}

impl<T> const ConstSlice<T> for [T] {
	fn const_copy_from_slice(&mut self, source: &Self) where T: Copy {
		if self.len() != source.len() {
			panic!("Invalid source (length not equal to destination length)");
		}

		unsafe {
			core::ptr::copy_nonoverlapping(source.as_ptr(), self.as_mut_ptr(), self.len());
		}
	}

	fn const_unroll<const N: usize>(&self) -> [T; N] where T: Copy + ~const Default {
		let mut tmp: [T; N] = [Default::default(); N];

		if self.len() < tmp.len() {
			panic!("Invalid source (length less than unroll count)");
		}

		let mut i: usize = 0;
		while i < tmp.len() {
			tmp[i] = self[i];
			i += 1;
		}

		tmp
	}
}

impl<T, const N: usize> const ConstSizedSlice<T, N> for [T; N] {
	fn const_concat<const M: usize>(&self, other: &[T; M]) -> [T; N + M] where T: Copy + ~const Default {
		let mut tmp: [T; N + M] = [Default::default(); N + M];

		let mut i: usize = 0;
		while i < self.len() {
			tmp[i] = self[i];
			i += 1;
		}

		let mut j: usize = 0;
		while j < other.len() {
			tmp[i + j] = other[j];
			j += 1;
		}

		tmp
	}
}
