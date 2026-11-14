# Quiet for every check: a `from_*` whose own type sits inside what it takes or
# gives, as `GitHubSignUp.from_claim` and `ConversationKey.from_url_path` have it.
Claim := Str.{
	from_mac : Str -> [Signed(Claim), Unsigned]
	from_mac = |text| Signed(Claim.(text))

	from_url_path : { path : Str } -> Try({ key : Claim, rest : Str }, [NotAPath])
	from_url_path = |{ path }| Ok({ key: Claim.(path), rest: "" })

	from_seal : { claim : Claim, mac : Str } -> Str
	from_seal = |{ mac }| mac

	from_lines : Str -> Try(List(Claim), [NoLines])
	from_lines = |text| Ok([Claim.(text)])
}
