## Quiet for name_hunt's `container`: where the container word is the
## domain's, its doc says whose, and a name that says what it holds.

## A folder's entry, as the host lists it.
Entry : { name : Str, size : U64 }

## A listing's entries named in text, and the names that were not.
Listing : { entries : List(Entry), not_utf8 : List(List(U8)) }

DmSummary : { partner : Str, unread : U64 }

FolderEntry : { name : Str }
