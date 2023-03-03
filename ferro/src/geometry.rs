#[derive(Default, Clone, Copy)]
pub struct Point {
	pub x: usize,
	pub y: usize,
}

impl Point {
	pub fn new(x: usize, y: usize) -> Self {
		Self { x, y }
	}

	pub fn is_within_rect(&self, rect: &Rect) -> bool {
		self.x >= rect.origin.x &&
		self.y >= rect.origin.y &&
		self.x < rect.origin.x + rect.size.width &&
		self.y < rect.origin.y + rect.size.height
	}

	pub fn as_index(&self, width: usize) -> usize {
		(self.y * width) + self.x
	}

	pub fn next_point(&self, bounds: &Size2D) -> Option<Self> {
		if self.y == bounds.width - 1 && self.x == bounds.height - 1 {
			None
		} else {
			Some(Self::new(if self.x == bounds.width - 1 { 0 } else { self.x + 1 }, if self.x == bounds.width - 1 { self.y + 1 } else { self.y }))
		}
	}
}

impl From<(usize, usize)> for Point {
	fn from(value: (usize, usize)) -> Self {
		Self::new(value.0, value.1)
	}
}

#[derive(Default, Clone, Copy)]
pub struct Size2D {
	pub width: usize,
	pub height: usize,
}

impl Size2D {
	pub fn new(width: usize, height: usize) -> Self {
		Self { width, height }
	}
}

impl From<(usize, usize)> for Size2D {
	fn from(value: (usize, usize)) -> Self {
		Self::new(value.0, value.1)
	}
}

#[derive(Default, Clone, Copy)]
pub struct Rect {
	/// top-left corner
	pub origin: Point,
	pub size: Size2D,
}

impl Rect {
	pub fn new(origin: Point, size: Size2D) -> Self {
		Self { origin, size }
	}

	pub fn new_from_points(first: Point, second: Point) -> Self {
		let width = (if first.x < second.x { second.x - first.x } else { first.x - second.x }) + 1;
		let height = (if first.y < second.y { second.y - first.y } else { first.y - second.y }) + 1;
		let x = if first.x < second.x { first.x } else { second.x };
		let y = if first.y < second.y { first.y } else { second.y };
		Self::new(Point::new(x, y), Size2D::new(width, height))
	}

	pub fn top_left(&self) -> Point {
		self.origin
	}

	pub fn top_right(&self) -> Point {
		Point::new(self.origin.x + self.size.width - 1, self.origin.y)
	}

	pub fn bottom_left(&self) -> Point {
		Point::new(self.origin.x, self.origin.y + self.size.height - 1)
	}

	pub fn bottom_right(&self) -> Point {
		Point::new(self.origin.x + self.size.width - 1, self.origin.y + self.size.height - 1)
	}

	pub fn is_within_rect(&self, containing_rect: &Self) -> bool {
		self.top_left().is_within_rect(containing_rect) && self.bottom_right().is_within_rect(containing_rect)
	}
}
