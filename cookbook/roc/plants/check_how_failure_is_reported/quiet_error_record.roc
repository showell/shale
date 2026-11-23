# Quiet for errors' `error_alias`: a record carried as the error (roc-chat's
# `Try(x, Chat.Answer)`, an early HTTP answer) is no alias of tags, named
# qualified or bare.
Chat := [].{
	Answer : { status : U16, body : List(U8) }
}

viewer! : Str => Try(Str, Chat.Answer)

Early : { status : U16 }

seen! : Str => Try(Str, Early)
