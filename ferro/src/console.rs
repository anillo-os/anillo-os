const FONT_DATA: &[u8] = include_bytes!("../resources/ter-u16n.psf");

#[repr(packed)]
struct ConsoleFont {
	magic: u32,
	version: u32,
	header_size: u32,
	flags: u32,
	glyph_count: u32,
	glyph_size: u32,
	glyph_height: u32,
	glyph_width: u32,
	glyphs: [u8; 0],
}
