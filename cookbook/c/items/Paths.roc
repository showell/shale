## How the C recipes read a file's path below the folder they walk: whether
## it is the tests', and whether it is under a folder named `out`, the
## build's output, which no recipe reads.
Paths := [].{

	## Whether `path`, below the folder, is the tests': in a `tests` folder,
	## or a file whose name starts with `test` (c/foundation/test.h, the
	## tests' runner).
	is_test_path : Str, Str -> Bool
	is_test_path = |folder, path| {
		below = Str.from_utf8_lossy(path.to_utf8().drop_first(folder.count_utf8_bytes() + 1))
		file_name = match below.split_on("/").last() {
			Ok(last) => last
			Err(_) => below
		}
		below.starts_with("tests/") or below.contains("/tests/") or file_name.starts_with("test")
	}

	## Whether a folder below `folder` on the way to `path` is named `out`.
	is_in_out_folder : Str, Str -> Bool
	is_in_out_folder = |folder, path|
		path.drop_prefix("${folder}/").split_on("/").drop_last(1).contains("out")
}
