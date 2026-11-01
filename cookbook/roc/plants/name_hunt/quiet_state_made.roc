# Quiet for name_hunt's `state_effect`: a participle that starts the name
# of the type an effect takes modifies what it draws (roc-ray's
# `rounded_rectangle!`), and a qualified type's name counts the same.
Frame := [].{}
RoundedRectangle : { x : F32, y : F32, radius : F32 }

rounded_rectangle! : Frame, RoundedRectangle => {}
rounded_rectangle! = |_frame, _cfg| {}

dashed_line! : Frame, Shapes.DashedLine => {}
dashed_line! = |_frame, _cfg| {}
