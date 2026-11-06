# Quiet for name_hunt: the platform chose this name, so the app is not asked to
# pick another.
app [loaded] { pf: platform "./platform/main.roc" }

loaded : U64 -> U64
loaded = |n| n
