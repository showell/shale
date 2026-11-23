# Quiet for every check: an invite system's acts, from roc-chat (mint,
# invite, redeem, audit, spend, echo) and their neighbors (grant, issue,
# subscribe, revoke, retire, sweep, touch), are verbs.
mint! : Str => Str
mint! = |code| code
invite! = |who| who
redeem! = |code| code
audit_invite! = |code| code
audit_grant! = |code| code
audit_use! = |code| code
spend_nonce! = |nonce| nonce
echo! = |text| text
grant_role! = |who| who
issue_token! = |who| who
subscribe_all! = |xs| xs
revoke_grant! = |who| who
retire_chat! = |id| id
sweep_stale! = |xs| xs
touch_seen! = |id| id
