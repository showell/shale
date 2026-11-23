# A plant for find_awkward_names's `bare_participle`: an app's own name is its own, even
# beside one its platform dictates.
app [loaded] { pf: platform "./platform/main.roc" }

loaded : U64 -> U64
loaded = |number| number

saved : U64 -> U64
saved = |number| number
