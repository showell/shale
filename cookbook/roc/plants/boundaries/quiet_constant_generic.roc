# A quiet plant for boundaries' `constant`: a function of `{}` alone whose
# type holds a type variable stays a function, since Roc refuses a generic
# constant ("value is not polymorphic").
empty_pair : {} -> (List(a), List(a))
empty_pair = |{}| ([], [])
