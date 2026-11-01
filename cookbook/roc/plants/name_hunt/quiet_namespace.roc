# Quiet for every check: a device's prefix (cookbook/words/namespaces.txt)
# and its act after it, a verb or the namespace's own instruction (x86's
# OUT).
Host := [].{
	block_read_sector! : U64 => List(U8)

	uefi_read_key! : {} => U16

	port_out_32! : U16, U32 => {}
}
