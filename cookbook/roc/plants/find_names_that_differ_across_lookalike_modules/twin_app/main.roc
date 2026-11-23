# An app, whose type name is the platform's to choose.
app [Model, main!] { pf: platform "./platform/main.roc" }

Model : { score : U64 }
