# A plant for name_hunt's `bare_participle`: an app's own name is its own, even
# beside one its platform dictates.
app [loaded] { pf: platform "./platform/main.roc" }

loaded : U64 -> U64
loaded = |n| n

saved : U64 -> U64
saved = |n| n
