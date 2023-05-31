pub(super) enum EntryType {
	/// An entry for a normal 4KiB page.
	Regular,
	/// An entry for a large 2MiB page.
	Large,
	/// An entry for a very large 1GiB page.
	VeryLarge,
	/// An entry that points to another page table.
	Table,
}
