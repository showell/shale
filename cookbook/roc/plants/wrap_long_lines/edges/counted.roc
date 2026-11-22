module [described, points, total]

# A comment line this long is counted, not listed: no trailing comma shortens a comment, however it is laid out.
points = [{ x: 0.1, y: 0.2 }, { x: 0.3, y: 0.4 }, { x: 0.5, y: 0.6 }, { x: 0.7, y: 0.8 }, { x: 0.9, y: 1.0 }]

described = "a string literal long enough that it alone takes this line past the width of one hundred"

# No group at all, so this line is listed, its text cut at 140 bytes.
total = alpha_value + beta_value + gamma_value + delta_value + epsilon_value + zeta_value + eta_value + theta_value + iota_value + kappa_value
