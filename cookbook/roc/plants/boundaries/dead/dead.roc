# A plant for boundaries' `dead`: an app's own function that nothing calls, as
# `end_game` in rocco-engine's pong.
app [start] { pf: platform "../plat/main.roc" }

start = |model| model

leftover = |model| model
