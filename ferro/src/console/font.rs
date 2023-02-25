use crate::util::slices_are_equal;

const FONT_DATA: &[u8] = include_bytes!("../../resources/ter-u16n.psf");

#[repr(packed)]
struct PSF1Header {
	flags: u8,
	glyph_size: u8,
}

#[repr(packed)]
struct PSF2Header {
	magic: u32,
	version: u32,
	header_size: u32,
	flags: u32,
	glyph_count: u32,
	glyph_size: u32,
	glyph_height: u32,
	glyph_width: u32,
}

const PSF1_MAGIC: &[u8] = &[0x36, 0x04];
const PSF2_MAGIC: &[u8] = &[0x72, 0xb5, 0x4a, 0x86];

const fn process_font(file_bytes: &'static [u8]) -> (&'static [u8], &'static [u8]) {
	let is_psf1: bool = file_bytes.len() > PSF1_MAGIC.len() && slices_are_equal(&file_bytes[0..2], PSF1_MAGIC);
	let is_psf2: bool = file_bytes.len() > PSF2_MAGIC.len() && slices_are_equal(&file_bytes[0..4], PSF2_MAGIC);
	assert!(is_psf1 || is_psf2);

	let mut version = 0
	let mut header_size = 0
	let mut flags = 0
	let mut glyph_count = 0
	let mut glyph_size = 0
	let mut glyph_height = 0
	let mut glyph_width = 0
}
