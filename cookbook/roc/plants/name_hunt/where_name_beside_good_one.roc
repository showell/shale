# A plant for name_hunt's `where_name`: one method of two is one word, and a
# method with a good name doesn't hide it.
scenery_size : a -> U64 where [a.scenery_height : a -> U64, a.width : a -> U64]
scenery_size = |thing| thing.scenery_height() + thing.width()
