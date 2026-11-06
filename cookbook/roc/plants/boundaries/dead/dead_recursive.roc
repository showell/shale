# A plant for boundaries' `dead`: a function whose only caller is itself.
app [begin] { pf: platform "../plat/main.roc" }

begin = |n| n

countdown = |n| countdown(n - 1)
